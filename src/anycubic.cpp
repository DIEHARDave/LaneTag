// Anycubic Kobra printers in LAN mode (Kobra 3 / S1 generation).
//
// With LAN mode on, the printer runs an MQTT broker (TLS, port 9883) whose
// login comes from a handshake on port 18910: GET /info gives a token, a
// signed POST to its ctrlInfoUrl answers with the login AES-encrypted under
// that token. Commands go to anycubic/anycubicCloud/v1/web/printer/<model>/
// <device>/<type>, reports come back under .../printer/public/<model>/
// <device>/<type>/report. The ACE is the "multiColorBox": getInfo reads it,
// setInfo sets a slot's material and color, as Anycubic Slicer Next does
// (handshake and topics as validated by alexmohr/anycubic_ha_local, the
// setInfo payload as anycubic-cloud-api sends it).
#include "anycubic.h"

#include <HTTPClient.h>
#include <MD5Builder.h>
#include <WiFi.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <time.h>
#include <vector>

#include "mqtt.h"

namespace {

constexpr uint16_t INFO_PORT = 18910;
constexpr uint32_t TIMEOUT = 8000;
const char *const TOPIC_PREFIX = "anycubic/anycubicCloud/v1";

String upper(String text) {
    text.trim();
    text.toUpperCase();
    return text;
}

String randomText(int length, const char *alphabet) {
    String out;
    size_t n = strlen(alphabet);
    for (int i = 0; i < length; i++) out += alphabet[esp_random() % n];
    return out;
}

String md5Hex(const String &text) {
    MD5Builder md5;
    md5.begin();
    md5.add(text);
    md5.calculate();
    return md5.toString();
}

// --- Clock ---------------------------------------------------------------------------
//
// The handshake is signed over the time in milliseconds. SNTP sets the clock
// when there is internet; the printer's own HTTP Date header covers a
// network without it.

int64_t dateSeconds(const String &date) {
    // "Thu, 22 Oct 2026 21:00:00 GMT"
    static const char *const MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int day, year, h, m, s;
    char month[4] = {};
    if (sscanf(date.c_str(), "%*3s, %d %3s %d %d:%d:%d", &day, month, &year, &h, &m, &s) != 6) return 0;
    const char *found = strstr(MONTHS, month);
    if (!found) return 0;
    int mon = (found - MONTHS) / 3 + 1;
    // Days from civil (Howard Hinnant).
    int y = year - (mon <= 2);
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + m * 60 + s;
}

int64_t nowMs(int64_t printerSeconds, uint32_t printerAt) {
    time_t t = time(nullptr);
    if (t > 1600000000) return (int64_t)t * 1000 + millis() % 1000;
    if (printerSeconds) return printerSeconds * 1000 + (millis() - printerAt);
    return 0;
}

// --- Handshake -----------------------------------------------------------------------

struct Broker {
    String modelId;
    String deviceId;
    String user;
    String password;
    uint16_t port = 9883;
};

bool httpJson(const String &url, const char *method, JsonDocument &doc, String &error, String *date = nullptr) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(TIMEOUT);
    if (!http.begin(client, url)) {
        error = "bad address " + url;
        return false;
    }
    const char *headers[] = {"Date"};
    http.collectHeaders(headers, 1);
    int code = http.sendRequest(method, (uint8_t *)nullptr, 0);
    String body = code > 0 ? http.getString() : String();
    if (date) *date = http.header("Date");
    http.end();
    if (code <= 0) {
        error = "no answer on port " + String(INFO_PORT) + " (is it a Kobra 3 / S1 in LAN mode?)";
        return false;
    }
    if (code != 200 || deserializeJson(doc, body)) {
        error = "the printer's LAN handshake answered HTTP " + String(code);
        return false;
    }
    return true;
}

// AES-128-CBC with PKCS7 padding, as the printer seals the login.
bool decryptLogin(const String &sealed, const String &token, const String &localToken, String &plain) {
    std::vector<uint8_t> data(sealed.length());
    size_t len = 0;
    if (mbedtls_base64_decode(data.data(), data.size(), &len, (const uint8_t *)sealed.c_str(), sealed.length()) ||
        !len || len % 16)
        return false;
    uint8_t key[16], iv[16] = {};
    memcpy(key, token.c_str() + 16, 16);
    memcpy(iv, localToken.c_str(), min((size_t)16, localToken.length()));
    std::vector<uint8_t> out(len);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 128);
    int err = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, data.data(), out.data());
    mbedtls_aes_free(&aes);
    uint8_t pad = out[len - 1];
    if (err || !pad || pad > 16) return false;
    plain = String((const char *)out.data(), len - pad);
    return true;
}

bool handshake(const PrinterConfig &printer, Broker &broker, String &error) {
    JsonDocument info;
    String date;
    String base = "http://" + printer.host + ":" + String(INFO_PORT);
    if (!httpJson(base + "/info", "GET", info, error, &date)) return false;
    uint32_t dateAt = millis();
    if (info["ctrlType"] == "cloud") {
        error = "the printer is in cloud mode; turn on LAN mode on its screen";
        return false;
    }
    String token = info["token"] | "", ctrlUrl = info["ctrlInfoUrl"] | "";
    broker.modelId = info["modelId"].as<String>();
    if (token.length() < 32 || !ctrlUrl.length() || broker.modelId == "null" || !broker.modelId.length()) {
        error = "this printer does not use the signed LAN handshake (Kobra 3 / S1 generation only)";
        return false;
    }
    int64_t ts = nowMs(dateSeconds(date), dateAt);
    if (!ts) {
        error = "the reader's clock is not set yet (no internet time, and the printer sent no date)";
        return false;
    }
    // The printer's port and path, on the address entered for it.
    int scheme = ctrlUrl.indexOf("://");
    String rest = scheme >= 0 ? ctrlUrl.substring(scheme + 3) : ctrlUrl;
    int slash = rest.indexOf('/');
    String hostPort = slash >= 0 ? rest.substring(0, slash) : rest;
    String path = slash >= 0 ? rest.substring(slash) : String("/");
    int colon = hostPort.indexOf(':');
    String port = colon >= 0 ? hostPort.substring(colon + 1) : String(INFO_PORT);
    String nonce = randomText(6, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    String did = randomText(32, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    char tsText[24];
    snprintf(tsText, sizeof(tsText), "%lld", (long long)ts);
    String sign = md5Hex(md5Hex(token.substring(0, 16)) + tsText + nonce);
    String url = "http://" + printer.host + ":" + port + path + (path.indexOf('?') >= 0 ? "&" : "?") +
                 "ts=" + tsText + "&nonce=" + nonce + "&sign=" + sign + "&did=" + did;
    JsonDocument ctrl;
    if (!httpJson(url, "POST", ctrl, error)) return false;
    if ((ctrl["code"] | 0) != 200) {
        error = "the printer refused the LAN handshake: " + String(ctrl["message"] | "?");
        return false;
    }
    String plain;
    if (!decryptLogin(ctrl["data"]["info"] | "", token, ctrl["data"]["token"] | "", plain)) {
        error = "could not read the printer's MQTT login";
        return false;
    }
    JsonDocument login;
    if (deserializeJson(login, plain)) {
        error = "could not read the printer's MQTT login";
        return false;
    }
    broker.user = login["username"] | "";
    broker.password = login["password"] | "";
    broker.deviceId = login["deviceId"] | "";
    String url2 = login["broker"] | "";
    int portAt = url2.lastIndexOf(':');
    if (portAt > 0) broker.port = url2.substring(portAt + 1).toInt();
    if (!broker.user.length() || !broker.deviceId.length() || !broker.port) {
        error = "the printer's MQTT login is incomplete";
        return false;
    }
    return true;
}

// --- MQTT -----------------------------------------------------------------------------

bool openPrinter(Mqtt &mqtt, const PrinterConfig &printer, Broker &broker, String &error) {
    if (!handshake(printer, broker, error)) return false;
    if (!mqtt.open(printer.host, broker.port, broker.user, broker.password) ||
        !mqtt.subscribe(String(TOPIC_PREFIX) + "/printer/public/" + broker.modelId + "/" + broker.deviceId + "/#")) {
        error = mqtt.error;
        return false;
    }
    return true;
}

bool command(Mqtt &mqtt, const Broker &broker, const char *type, const char *action, JsonDocument *data = nullptr) {
    JsonDocument msg;
    msg["type"] = type;
    msg["action"] = action;
    char ts[24];
    snprintf(ts, sizeof(ts), "%lld", (long long)nowMs(0, 0));
    msg["timestamp"] = serialized(ts);
    msg["msgid"] = randomText(32, "0123456789abcdef");
    if (data) msg["data"] = *data;
    else msg["data"] = nullptr;
    return mqtt.publish(String(TOPIC_PREFIX) + "/web/printer/" + broker.modelId + "/" + broker.deviceId + "/" + type, msg);
}

void reportFilter(JsonDocument &filter) {
    for (const char *key : {"type", "action", "code", "msg", "state"}) filter[key] = true;
    filter["data"]["state"] = true;
    JsonObject box = filter["data"]["multi_color_box"][0].to<JsonObject>();
    box["id"] = true;
    JsonObject slot = box["slots"][0].to<JsonObject>();
    for (const char *key : {"index", "type", "color", "status"}) slot[key] = true;
}

struct AceState {
    JsonDocument box;       // the reader's ACE, from multiColorBox
    bool haveBox = false;
    String printer;         // info.state: "free" or "busy"
};

// Asks for the ACE (and the printer's state) and collects the answers.
bool readAce(Mqtt &mqtt, const PrinterConfig &printer, const Broker &broker, AceState &state, String &error) {
    if (!command(mqtt, broker, "multiColorBox", "getInfo") || !command(mqtt, broker, "info", "query")) {
        error = mqtt.error;
        return false;
    }
    JsonDocument filter, msg;
    reportFilter(filter);
    uint32_t deadline = millis() + TIMEOUT, settle = 0;
    while (mqtt.receive(msg, filter, deadline)) {
        String type = msg["type"] | "";
        if (type == "info" && msg["data"]["state"].is<const char *>()) state.printer = msg["data"]["state"].as<String>();
        if (type == "multiColorBox") {
            for (JsonObjectConst box : msg["data"]["multi_color_box"].as<JsonArrayConst>()) {
                if ((box["id"] | -1) != printer.unit - 1 || !box["slots"].is<JsonArrayConst>()) continue;
                if (!state.haveBox) {
                    state.box.set(box);
                } else {
                    // Partial reports carry only some slots: merge them in.
                    for (JsonObjectConst s : box["slots"].as<JsonArrayConst>()) {
                        bool merged = false;
                        for (JsonObject old : state.box["slots"].as<JsonArray>())
                            if ((old["index"] | -1) == (s["index"] | -2)) old.set(s), merged = true;
                        if (!merged) state.box["slots"].add(s);
                    }
                }
                state.haveBox = true;
            }
        }
        if (state.haveBox) {
            if (state.printer.length()) return true;
            if (!settle) settle = millis() + 1500;
            deadline = settle;
        }
    }
    if (state.haveBox) return true;
    error = mqtt.error.length() ? mqtt.error
                                : "the printer did not report ACE " + String(printer.unit) + " (is it connected?)";
    return false;
}

JsonObjectConst findSlot(const AceState &state, int slot) {
    for (JsonObjectConst s : state.box["slots"].as<JsonArrayConst>())
        if ((s["index"] | -1) == slot - 1) return s;
    return JsonObjectConst();
}

String rgbHex(JsonArrayConst rgb) {
    if (rgb.size() < 3) return "";
    char hex[8];
    snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb[0].as<int>() & 0xFF, rgb[1].as<int>() & 0xFF, rgb[2].as<int>() & 0xFF);
    return hex;
}

SlotInfo slotInfo(const PrinterConfig &printer, int slot, JsonObjectConst s) {
    SlotInfo info;
    info.slot = slot;
    info.label = slotName(printer, slot);
    info.material = s["type"] | "";
    info.color = info.material.length() ? rgbHex(s["color"]) : String();
    info.loaded = info.material.length() > 0;
    return info;
}

// The ACE's own material names (what Anycubic Slicer Next can set), or ""
// for one the ACE does not take.
String aceMaterial(const Filament &f) {
    String m = upper(f.material.length() ? f.material : String("PLA"));
    if (m.startsWith("TPU") || m.startsWith("TPE")) return "";
    if (m.startsWith("PA") && m.indexOf("CF") > 0) return "PACF";
    if (m.startsWith("PCTG") || m.startsWith("PET")) return "PETG";
    if (m == "PC" || m.startsWith("PC-") || m.startsWith("PC ")) return "PC";
    for (const char *known : {"ASA", "ABS", "HIPS", "PA"})
        if (m.startsWith(known)) return known;
    if (m.indexOf("PLA") >= 0) return "PLA";
    return "";
}

}  // namespace

SlotList anycubicSlots(const PrinterConfig &printer) {
    SlotList list;
    Mqtt mqtt;
    Broker broker;
    AceState state;
    if (!openPrinter(mqtt, printer, broker, list.error) || !readAce(mqtt, printer, broker, state, list.error)) return list;
    for (int slot = 1; slot <= printer.slotCount(); slot++) {
        JsonObjectConst s = findSlot(state, slot);
        if (s.isNull()) continue;
        list.slots.push_back(slotInfo(printer, slot, s));
    }
    if (list.slots.empty()) {
        list.error = "the printer reported ACE " + String(printer.unit) + " without slots";
        return list;
    }
    list.ok = true;
    return list;
}

PrinterReply anycubicAssign(const PrinterConfig &printer, const Filament &f, int slot) {
    PrinterReply reply;
    if (slot < 1 || slot > printer.slotCount()) {
        reply.message = "no such slot";
        return reply;
    }
    if (!f.hasFilamentData()) {
        reply.message = f.spoolId ? "The tag only names Spoolman spool " + String(f.spoolId) +
                                        "; Anycubic printers cannot look it up"
                                  : "No filament data on this tag";
        return reply;
    }
    String material = aceMaterial(f);
    if (!material.length()) {
        reply.message = f.material + " is not a material the ACE takes";
        return reply;
    }
    Mqtt mqtt;
    Broker broker;
    AceState state;
    if (!openPrinter(mqtt, printer, broker, reply.message) || !readAce(mqtt, printer, broker, state, reply.message))
        return reply;
    if (state.printer == "busy") {
        reply.message = "The printer is busy (printing or feeding); slots are not changed now";
        return reply;
    }
    String label = slotName(printer, slot);
    uint32_t color = f.color >= 0 ? f.color & 0xFFFFFF : 0xFFFFFF;
    JsonDocument data;
    JsonObject box = data["multi_color_box"].add<JsonObject>();
    box["id"] = printer.unit - 1;
    JsonObject s = box["slots"].add<JsonObject>();
    s["index"] = slot - 1;
    s["type"] = material;
    JsonArray rgb = s["color"].to<JsonArray>();
    rgb.add((color >> 16) & 0xFF);
    rgb.add((color >> 8) & 0xFF);
    rgb.add(color & 0xFF);
    if (!command(mqtt, broker, "multiColorBox", "setInfo", &data)) {
        reply.message = mqtt.error;
        return reply;
    }
    // The printer answers on the multiColorBox report (code 200 when done).
    JsonDocument filter, msg;
    reportFilter(filter);
    uint32_t deadline = millis() + 4000;
    while (mqtt.receive(msg, filter, deadline)) {
        if (msg["type"] == "multiColorBox" && msg["action"] == "setInfo") {
            int code = msg["code"] | 200;
            if (code != 200) {
                reply.message = "The printer refused it: " + String(msg["msg"] | ("code " + String(code)).c_str());
                return reply;
            }
            break;
        }
    }
    AceState after;
    if (!readAce(mqtt, printer, broker, after, reply.message)) return reply;
    mqtt.close();
    JsonObjectConst now = findSlot(after, slot);
    SlotInfo info = slotInfo(printer, slot, now);
    char hex[8];
    snprintf(hex, sizeof(hex), "%06X", (unsigned)(color & 0xFFFFFF));
    if (now.isNull() || info.material != material || info.color != hex) {
        reply.message = label + " is still " + (info.material.length() ? info.material + " #" + info.color : "empty") +
                        "; the printer did not take the change";
        return reply;
    }
    reply.ok = true;
    reply.title = f.title();
    reply.message = label + " is now " + material + " #" + info.color;
    if (material != upper(f.material)) reply.message += " (the ACE's name for " + f.material + ")";
    return reply;
}
