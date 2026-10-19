#include "printers.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <mbedtls/base64.h>

#include "decoders.h"

namespace {

constexpr uint32_t TIMEOUT = 8000;
// The plugin may look the tag up in Spoolman before it answers.
constexpr uint32_t SCAN_TIMEOUT = 25000;
// Larger "spool IDs" on tags are not Spoolman's (phone apps write
// timestamps there).
constexpr int MAX_SPOOLMAN_ID = 10000000;

// --- HTTP --------------------------------------------------------------------

struct HttpResult {
    int code = 0;
    String body;
    bool ok() const { return code >= 200 && code < 300; }
};

HttpResult request(const String &url, const char *method, const String &body, const String &token = "",
                   uint32_t timeout = TIMEOUT) {
    HttpResult result;
    if (WiFi.status() != WL_CONNECTED) {
        result.code = -1;
        result.body = "Wi-Fi is not connected";
        return result;
    }
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(timeout);
    if (!http.begin(client, url)) {
        result.code = -1;
        result.body = "bad address " + url;
        return result;
    }
    if (body.length()) http.addHeader("Content-Type", "application/json");
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    result.code = http.sendRequest(method, (uint8_t *)body.c_str(), body.length());
    result.body = result.code > 0 ? http.getString() : HTTPClient::errorToString(result.code);
    http.end();
    return result;
}

String describeFailure(const HttpResult &result, const JsonDocument &doc) {
    if (result.code <= 0) return result.body;
    const char *error = doc["error"]["message"] | (doc["error"] | (const char *)nullptr);
    if (error) return error;
    return "HTTP " + String(result.code);
}

String base64(const uint8_t *data, size_t len) {
    if (!len) return "";
    size_t outLen = 0;
    std::vector<uint8_t> out(4 * ((len + 2) / 3) + 1);
    mbedtls_base64_encode(out.data(), out.size(), &outLen, data, len);
    return String((const char *)out.data(), outLen);
}

// --- Materials and colours -----------------------------------------------------

String upper(String text) {
    text.trim();
    text.toUpperCase();
    return text;
}

// Zmod material type for a tag's material: exact match, then the base
// without filler or modifiers, then the fallbacks zmod_nfc uses.
String resolveMaterial(const String &input, JsonArrayConst validTypes) {
    String material = upper(input);
    std::vector<String> valid;
    for (JsonVariantConst t : validTypes) {
        String type = upper(t.as<String>());
        if (type.length() && type != "NONE" && type != "N/A" && type != "?") valid.push_back(type);
    }
    if (valid.empty()) return material;
    auto has = [&](const String &m) {
        for (auto &v : valid)
            if (v == m) return true;
        return false;
    };
    int dash = material.indexOf('-'), space = material.indexOf(' ');
    String candidates[] = {material, dash > 0 ? material.substring(0, dash) : material,
                           space > 0 ? material.substring(0, space) : material};
    for (auto &c : candidates)
        if (has(c)) return c;
    String fallback;
    if (material.startsWith("ASA")) fallback = "ABS";
    else if (material == "COPE" || material == "CPE" || material.indexOf("PLA") >= 0) fallback = "PLA";
    else if (material.startsWith("PCTG") || material.startsWith("PET")) fallback = "PETG";
    return has(fallback) ? fallback : "";
}

void rgbOf(uint32_t color, long &r, long &g, long &b) {
    r = (color >> 16) & 0xFF;
    g = (color >> 8) & 0xFF;
    b = color & 0xFF;
}

// Closest palette entry by the "redmean" weighted RGB distance (as zmod_nfc
// and zmod_afclite snap colours to Zmod's palette).
String nearestPalette(uint32_t color, JsonArrayConst palette) {
    String best;
    double bestDistance = 1e18;
    long r1, g1, b1;
    rgbOf(color, r1, g1, b1);
    for (JsonVariantConst entry : palette) {
        int32_t c = parseColor(entry.as<String>());
        if (c < 0) continue;
        long r2, g2, b2;
        rgbOf(c, r2, g2, b2);
        double mean = (r1 + r2) / 2.0;
        double d = (2 + mean / 256) * (r1 - r2) * (r1 - r2) + 4 * (g1 - g2) * (g1 - g2) +
                   (2 + (255 - mean) / 256) * (b1 - b2) * (b1 - b2);
        if (d < bestDistance) {
            bestDistance = d;
            char hex[12];
            snprintf(hex, sizeof(hex), "%06X", (unsigned)(c & 0xFFFFFF));
            best = hex;
        }
    }
    return best;
}

// The Creator 5's stock firmware shows a slot colour only when it is one
// of its palette entries, byte for byte (with '#'); materials from its list.
const char *const C5_PALETTE[] = {
    "#FFFFFF", "#FFF245", "#DEF578", "#21CC3D", "#167A4B", "#156682", "#24E4A0", "#7BD9F0",
    "#4CAAF8", "#2E54DD", "#48358C", "#A341F7", "#F435F6", "#D5B4DE", "#FA6173", "#F82D29",
    "#805003", "#F9903B", "#FCEBD7", "#D5C5A1", "#B17C38", "#8C8C89", "#BEBEBE", "#1B1B1B",
};
const char *const C5_MATERIALS[] = {
    "PLA", "PETG", "PLA-CF", "PETG-CF", "ABS", "ASA", "SILK", "PET-CF", "PAHT-CF", "S-PAHT",
    "S-Multi", "PA-CF", "HIPS", "PVA", "TPU-90A", "TPU-95A", "TPU-64D", "PC", "PA", "PC-ABS", "PPS-CF",
};

void toLab(uint32_t color, double lab[3]) {
    double c[3];
    for (int i = 0; i < 3; i++) {
        double v = ((color >> (16 - 8 * i)) & 0xFF) / 255.0;
        c[i] = v > 0.04045 ? pow((v + 0.055) / 1.055, 2.4) : v / 12.92;
    }
    double x = (c[0] * 0.4124 + c[1] * 0.3576 + c[2] * 0.1805) / 0.95047;
    double y = c[0] * 0.2126 + c[1] * 0.7152 + c[2] * 0.0722;
    double z = (c[0] * 0.0193 + c[1] * 0.1192 + c[2] * 0.9505) / 1.08883;
    auto f = [](double v) { return v > 0.008856 ? cbrt(v) : 7.787 * v + 16.0 / 116.0; };
    lab[0] = 116 * f(y) - 16;
    lab[1] = 500 * (f(x) - f(y));
    lab[2] = 200 * (f(y) - f(z));
}

String nearestC5Color(uint32_t color) {
    double source[3];
    toLab(color, source);
    const char *best = C5_PALETTE[0];
    double bestDistance = 1e18;
    for (const char *entry : C5_PALETTE) {
        double lab[3];
        toLab(strtol(entry + 1, nullptr, 16), lab);
        double d = 0;
        for (int i = 0; i < 3; i++) d += (source[i] - lab[i]) * (source[i] - lab[i]);
        if (d < bestDistance) {
            bestDistance = d;
            best = entry;
        }
    }
    return best;
}

String c5Material(const Filament &f) {
    String material = upper(f.material);
    String mods = upper(f.modifiers);
    if (material == "PLA" && mods.indexOf("SILK") >= 0) return "SILK";
    if (material == "TPU") return mods.indexOf("90A") >= 0 ? "TPU-90A" : "TPU-95A";
    int dash = material.indexOf('-');
    String candidates[] = {material, dash > 0 ? material.substring(0, dash) : material};
    for (auto &c : candidates)
        for (const char *known : C5_MATERIALS)
            if (c.equalsIgnoreCase(known)) return known;
    if (material.startsWith("ASA")) return "ASA";
    if (material.indexOf("PLA") >= 0) return "PLA";
    return "";
}

String slotLabel(bool toolchanger, int slot) {
    return toolchanger ? "T" + String(slot - 1) : "Slot " + String(slot);
}

// --- zmod_nfc ------------------------------------------------------------------

String scanBody(const Tag &tag, const String &readerName) {
    JsonDocument doc;
    doc["reader"] = readerName;
    doc["uid"] = tag.uidHex();
    doc["kind"] = tagKindName(tag.kind);
    doc["sak"] = tag.sak;
    doc["atqa"] = toHex(tag.atqa, 2);
    doc["memory"] = base64(tag.memory.data(), tag.memory.size());
    if (tag.kind == TagKind::Type4) doc["ndef"] = base64(tag.ndef.data(), tag.ndef.size());
    if (tag.kind == TagKind::MifareClassic) {
        JsonObject keys = doc["sector_keys"].to<JsonObject>();
        for (int s = 0; s < MAX_SECTORS; s++)
            if (tag.sectorRead(s)) keys[String(s)] = tag.sectorKey[s];
    }
    doc["incomplete"] = tag.incomplete;
    String body;
    serializeJson(doc, body);
    return body;
}

PrinterReply zmodNfcScan(const PrinterConfig &printer, const Tag &tag, const String &readerName) {
    PrinterReply reply;
    HttpResult result = request(printer.baseUrl() + "/api/scan", "POST", scanBody(tag, readerName),
                                printer.token, SCAN_TIMEOUT);
    JsonDocument doc;
    deserializeJson(doc, result.body);
    if (!result.ok() || !(doc["ok"] | false)) {
        reply.message = describeFailure(result, doc);
        if (result.code == 401) reply.message = "the printer wants a different token (remote_token)";
        if (result.code < 0) reply.message += " - is zmod_nfc installed and up to date?";
        return reply;
    }
    reply.ok = true;
    reply.outcome = doc["outcome"] | "";
    reply.title = doc["scan"]["title"] | "";
    reply.spoolId = doc["scan"]["spool_id"] | 0;
    String spoolman = doc["spoolman"] | "";
    if (reply.outcome == "prompt")
        reply.message = "Pick the slot on the printer's screen (or below)";
    else if (reply.outcome == "assigned")
        reply.message = "Assigned to the printer's only slot";
    else if (reply.outcome == "disabled")
        reply.ok = false, reply.message = "zmod_nfc is disabled on the printer (NFC_ENABLE turns it on)";
    else if (reply.outcome == "printing")
        reply.ok = false, reply.message = "The printer is printing; zmod_nfc ignores tags while printing";
    else if (reply.outcome == "no_data")
        reply.ok = false, reply.message = "The printer found no filament data on this tag";
    else if (reply.outcome == "incomplete")
        reply.ok = false, reply.message = "The tag left the reader too soon; hold it still and scan again";
    else
        reply.message = reply.outcome;
    if (spoolman.length() && reply.ok) reply.message += " (Spoolman: " + spoolman + ")";
    return reply;
}

PrinterReply zmodNfcAssign(const PrinterConfig &printer, const Tag &tag, int slot, const String &readerName) {
    PrinterReply reply;
    String body = "{\"slot\":" + String(slot) + "}";
    for (int attempt = 0; attempt < 2; attempt++) {
        HttpResult result = request(printer.baseUrl() + "/api/assign", "POST", body, printer.token);
        JsonDocument doc;
        deserializeJson(doc, result.body);
        if (result.ok() && (doc["ok"] | false)) {
            reply.ok = true;
            reply.message = doc["message"] | "Assigned";
            reply.title = doc["scan"]["title"] | "";
            return reply;
        }
        reply.message = describeFailure(result, doc);
        // The printer's prompt timed out or was dismissed: send the tag again.
        if (attempt == 0 && result.ok() && reply.message.indexOf("no scanned tag") >= 0) {
            PrinterReply scan = zmodNfcScan(printer, tag, readerName);
            if (scan.ok && scan.outcome == "prompt") continue;
            return scan;
        }
        break;
    }
    return reply;
}

SlotList zmodNfcSlots(const PrinterConfig &printer) {
    SlotList list;
    HttpResult result = request(printer.baseUrl() + "/api/info", "GET", "", printer.token);
    JsonDocument doc;
    deserializeJson(doc, result.body);
    if (!result.ok()) {
        list.error = describeFailure(result, doc);
        return list;
    }
    for (JsonObjectConst s : doc["slots"].as<JsonArrayConst>()) {
        SlotInfo info;
        info.slot = s["slot"] | 0;
        info.label = s["label"] | "";
        info.material = s["material"] | "";
        info.color = s["color"] | "";
        info.loaded = s["loaded"] | false;
        list.slots.push_back(info);
    }
    list.ok = true;
    return list;
}

// --- Zmod through Moonraker ------------------------------------------------------

bool zmodColor(const PrinterConfig &printer, JsonDocument &status, String &error) {
    HttpResult result = request(printer.baseUrl() + "/printer/objects/query?zmod_color", "GET", "");
    JsonDocument doc;
    if (deserializeJson(doc, result.body) || !result.ok()) {
        error = describeFailure(result, doc);
        return false;
    }
    JsonObjectConst zmod = doc["result"]["status"]["zmod_color"];
    if (zmod.isNull() || zmod.size() == 0) {
        error = "Zmod's zmod_color is not loaded on this printer";
        return false;
    }
    status.set(zmod);
    return true;
}

SlotList basicSlots(const PrinterConfig &printer) {
    SlotList list;
    JsonDocument status;
    if (!zmodColor(printer, status, list.error)) return list;
    bool toolchanger = (status["total_tools"] | 1) > 1;
    int count = constrain(status["color_limit"] | 4, 1, 16);
    for (int n = 1; n <= count; n++) {
        SlotInfo info;
        info.slot = n;
        info.label = slotLabel(toolchanger, n);
        for (JsonObjectConst s : status["slots"].as<JsonArrayConst>()) {
            if ((s["ID"] | 0) != n) continue;
            info.material = s["Material"] | "";
            String hex = s["HEX"] | "";
            hex.replace("#", "");
            info.color = hex.substring(0, 6);
            info.loaded = s["hasFilament"] | false;
        }
        list.slots.push_back(info);
    }
    list.ok = true;
    return list;
}

// Moonraker lane_data, read by OrcaSlicer: the slot's entry (found by its
// "lane" field, as keys differ between printers) gets the spool's details;
// other writers' keys stay.
String writeLaneData(const PrinterConfig &printer, int slot, const Tag &tag, const Filament &f,
                     const String &material, const String &color) {
    String url = printer.baseUrl() + "/server/database/item";
    HttpResult current = request(url + "?namespace=lane_data", "GET", "");
    JsonDocument entries;
    if (current.ok()) deserializeJson(entries, current.body);
    JsonObjectConst value = entries["result"]["value"];
    String lane = String(slot - 1), key = "lane" + String(slot);
    for (JsonPairConst entry : value) {
        if (entry.value()["lane"].as<String>() == lane) {
            key = entry.key().c_str();
            break;
        }
    }
    JsonDocument doc;
    doc["namespace"] = "lane_data";
    doc["key"] = key;
    JsonObject out = doc["value"].to<JsonObject>();
    if (value[key].is<JsonObjectConst>()) out.set(value[key].as<JsonObjectConst>());
    out["lane"] = lane;
    out["material"] = material;
    out["color"] = "#" + color;
    if (f.spoolId > 0 && f.spoolId < MAX_SPOOLMAN_ID) out["spool_id"] = f.spoolId;
    else out.remove("spool_id");
    if (f.brand.length()) out["vendor"] = f.brand, out["vendor_name"] = f.brand;
    else out.remove("vendor"), out.remove("vendor_name");
    if (f.nozzleTemp()) out["nozzle_temp"] = f.nozzleTemp();
    else out.remove("nozzle_temp");
    if (f.bedTemp) out["bed_temp"] = f.bedTemp;
    else out.remove("bed_temp");
    out["zmod_nfc_name"] = f.title();
    out["zmod_nfc_uid"] = tag.uidHex();
    String body;
    serializeJson(doc, body);
    HttpResult result = request(url, "POST", body);
    return result.ok() ? "" : "lane_data not written (" + String(result.code) + ")";
}

PrinterReply basicAssign(const PrinterConfig &printer, const Tag &tag, const Filament &f, int slot) {
    PrinterReply reply;
    if (!f.hasFilamentData()) {
        reply.message = f.spoolId ? "The tag only names Spoolman spool " + String(f.spoolId) +
                                        "; the zmod_nfc method looks those up"
                                  : "No filament data on this tag";
        return reply;
    }
    JsonDocument status;
    if (!zmodColor(printer, status, reply.message)) return reply;
    String label = slotLabel((status["total_tools"] | 1) > 1, slot);
    String current, currentColor = "FFFFFF";
    for (JsonObjectConst s : status["slots"].as<JsonArrayConst>())
        if ((s["ID"] | 0) == slot) {
            current = s["Material"] | "";
            String hex = s["HEX"] | "FFFFFF";
            hex.replace("#", "");
            currentColor = hex.substring(0, 6);
        }
    String material = resolveMaterial(f.material, status["valid_types"]);
    String notes;
    if (!material.length()) {
        if (!current.length() || current == "?" || current == "NONE") {
            reply.message = f.material + " is not a material this printer knows";
            return reply;
        }
        material = current;
        notes += f.material + " is unknown to the printer; kept " + current + ". ";
    }
    String color = f.color >= 0 ? f.colorHex() : currentColor;
    JsonArrayConst palette = status["palette"];
    if (palette.size()) {
        String snapped = nearestPalette(strtol(color.c_str(), nullptr, 16), palette);
        if (snapped.length() && snapped != color) {
            notes += "#" + color + " stored as palette colour #" + snapped + ". ";
            color = snapped;
        }
    }
    JsonDocument cmd;
    cmd["script"] = "CHANGE_ZCOLOR SLOT=" + String(slot) + " HEX=" + color + " TYPE=" + material + " SILENT=1";
    String body;
    serializeJson(cmd, body);
    HttpResult result = request(printer.baseUrl() + "/printer/gcode/script", "POST", body, "", 15000);
    if (!result.ok()) {
        JsonDocument doc;
        deserializeJson(doc, result.body);
        reply.message = describeFailure(result, doc);
        return reply;
    }
    String lane = writeLaneData(printer, slot, tag, f, material, color);
    reply.ok = true;
    reply.message = label + " is now " + material + " #" + color + ". " + notes + lane;
    reply.message.trim();
    return reply;
}

// --- Stock Flashforge firmware -------------------------------------------------

void stockAuth(const PrinterConfig &printer, JsonDocument &doc) {
    doc["serialNumber"] = printer.serial;
    doc["checkCode"] = printer.checkCode;
}

bool stockCall(const PrinterConfig &printer, const char *path, JsonDocument &body, JsonDocument &reply,
               String &error) {
    String text;
    serializeJson(body, text);
    HttpResult result = request(printer.baseUrl() + path, "POST", text);
    if (deserializeJson(reply, result.body) || !result.ok()) {
        error = describeFailure(result, reply);
        return false;
    }
    int code = reply["code"] | -1;
    if (code != 0) {
        error = reply["message"] | ("the printer answered code " + String(code)).c_str();
        if (error.indexOf("heck") >= 0 || code == 1) error += " (check the serial number and check code)";
        return false;
    }
    return true;
}

SlotList stockSlots(const PrinterConfig &printer) {
    SlotList list;
    JsonDocument body, reply;
    stockAuth(printer, body);
    if (!stockCall(printer, "/detail", body, reply, list.error)) return list;
    JsonObjectConst station = reply["detail"]["matlStationInfo"];
    for (JsonObjectConst s : station["slotInfos"].as<JsonArrayConst>()) {
        SlotInfo info;
        info.slot = s["slotId"] | 0;
        info.label = "Slot " + String(info.slot);
        info.material = s["materialName"] | "";
        String color = s["materialColor"] | "";
        color.replace("#", "");
        info.color = color;
        info.loaded = s["hasFilament"] | false;
        list.slots.push_back(info);
    }
    if (list.slots.empty())
        for (int n = 1; n <= 4; n++) {
            SlotInfo info;
            info.slot = n;
            info.label = "Slot " + String(n);
            list.slots.push_back(info);
        }
    list.ok = true;
    return list;
}

PrinterReply stockAssign(const PrinterConfig &printer, const Filament &f, int slot) {
    PrinterReply reply;
    if (!f.hasFilamentData()) {
        reply.message = f.spoolId ? "The tag only names Spoolman spool " + String(f.spoolId) +
                                        "; stock firmware cannot look it up"
                                  : "No filament data on this tag";
        return reply;
    }
    String material, rgb;
    if (printer.model == Model::Creator5) {
        material = c5Material(f);
        if (!material.length()) {
            reply.message = f.material + " is not a Creator 5 material";
            return reply;
        }
        rgb = nearestC5Color(f.color >= 0 ? f.color : 0xFFFFFF);
    } else {
        material = upper(f.material.length() ? f.material : "PLA");
        rgb = f.color >= 0 ? f.colorHex() : "FFFFFF";
    }
    JsonDocument body, response;
    stockAuth(printer, body);
    body["payload"]["cmd"] = "msConfig_cmd";
    JsonObject args = body["payload"]["args"].to<JsonObject>();
    args["slot"] = slot;
    args["mt"] = material;
    args["rgb"] = rgb;
    if (!stockCall(printer, "/control", body, response, reply.message)) return reply;
    reply.ok = true;
    reply.message = "Slot " + String(slot) + " is now " + material + " " + (rgb.startsWith("#") ? "" : "#") + rgb;
    return reply;
}

}  // namespace

PrinterReply sendScan(const PrinterConfig &printer, const Tag &tag, const String &readerName) {
    if (printer.method == Method::ZmodNfc) return zmodNfcScan(printer, tag, readerName);
    PrinterReply reply;
    reply.message = "only the zmod_nfc method takes whole scans";
    return reply;
}

PrinterReply assignSlot(const PrinterConfig &printer, const Tag &tag, const Filament &filament, int slot,
                        const String &readerName) {
    switch (printer.method) {
    case Method::ZmodNfc: return zmodNfcAssign(printer, tag, slot, readerName);
    case Method::ZmodBasic: return basicAssign(printer, tag, filament, slot);
    default: return stockAssign(printer, filament, slot);
    }
}

void cancelPrompt(const PrinterConfig &printer) {
    if (printer.method == Method::ZmodNfc) request(printer.baseUrl() + "/api/cancel", "POST", "{}", printer.token);
}

SlotList listSlots(const PrinterConfig &printer) {
    switch (printer.method) {
    case Method::ZmodNfc: return zmodNfcSlots(printer);
    case Method::ZmodBasic: return basicSlots(printer);
    default: return stockSlots(printer);
    }
}

PrinterReply testPrinter(const PrinterConfig &printer) {
    PrinterReply reply;
    if (printer.method == Method::ZmodNfc) {
        HttpResult ping = request(printer.baseUrl() + "/api/ping", "GET", "");
        if (!ping.ok()) {
            reply.message = "zmod_nfc's listener does not answer on port " + String(printer.effectivePort()) +
                            " (" + (ping.code > 0 ? "HTTP " + String(ping.code) : ping.body) +
                            "). Update zmod_nfc and check remote_port.";
            return reply;
        }
    }
    SlotList slots = listSlots(printer);
    reply.ok = slots.ok;
    reply.message = slots.ok ? "Connected; " + String(slots.slots.size()) + " slots" : slots.error;
    return reply;
}

void slotListToJson(const SlotList &list, JsonObject out) {
    out["ok"] = list.ok;
    if (!list.ok) out["error"] = list.error;
    JsonArray slots = out["slots"].to<JsonArray>();
    for (auto &s : list.slots) {
        JsonObject o = slots.add<JsonObject>();
        o["slot"] = s.slot;
        o["label"] = s.label;
        o["material"] = s.material;
        o["color"] = s.color;
        o["loaded"] = s.loaded;
    }
}
