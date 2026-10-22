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

HttpResult request(const String &url, const char *method, const String &body, const String &apiKey = "",
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
    if (apiKey.length()) http.addHeader("X-Api-Key", apiKey);
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

// --- Materials and colors -----------------------------------------------------

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
// and zmod_afclite snap colors to Zmod's palette).
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

// The stock screens draw a slot's color only when it is one of their 24
// palette entries, byte for byte as "#RRGGBB"; any other color is stored
// but not shown. Each model has its own palette.
constexpr size_t PALETTE_SIZE = 24;
const char *const C5_PALETTE[PALETTE_SIZE] = {
    "#FFFFFF", "#FFF245", "#DEF578", "#21CC3D", "#167A4B", "#156682", "#24E4A0", "#7BD9F0",
    "#4CAAF8", "#2E54DD", "#48358C", "#A341F7", "#F435F6", "#D5B4DE", "#FA6173", "#F82D29",
    "#805003", "#F9903B", "#FCEBD7", "#D5C5A1", "#B17C38", "#8C8C89", "#BEBEBE", "#1B1B1B",
};
// From the AD5X's stock firmwareExe (3.1.7), as its screen stores them.
const char *const AD5X_PALETTE[PALETTE_SIZE] = {
    "#FFFFFF", "#FEF043", "#DCF478", "#0ACC38", "#067749", "#0C6283", "#0DE2A0", "#75D9F3",
    "#45A8F9", "#2750E0", "#46328E", "#A03CF7", "#F330F9", "#D4B0DC", "#F95D73", "#F72224",
    "#7C4B00", "#F98D33", "#FDEBD5", "#D3C4A3", "#AF7836", "#898989", "#BCBCBC", "#161616",
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

// CIEDE2000 color difference of two CIELAB colors. Plain CIELAB distance
// sends saturated blues to purple (#0022FF became "bright purple" on the
// AD5X palette); CIEDE2000 corrects the hue in that region.
double deltaE2000(const double lab1[3], const double lab2[3]) {
    const double deg = M_PI / 180;
    double c1 = hypot(lab1[1], lab1[2]), c2 = hypot(lab2[1], lab2[2]);
    double cBar7 = pow((c1 + c2) / 2, 7);
    double g = 0.5 * (1 - sqrt(cBar7 / (cBar7 + 6103515625.0)));    // 25^7
    double a1 = (1 + g) * lab1[1], a2 = (1 + g) * lab2[1];
    double c1p = hypot(a1, lab1[2]), c2p = hypot(a2, lab2[2]);
    double h1 = fmod(atan2(lab1[2], a1) / deg + 360, 360);
    double h2 = fmod(atan2(lab2[2], a2) / deg + 360, 360);
    double dL = lab2[0] - lab1[0], dC = c2p - c1p, dh = 0;
    if (c1p * c2p != 0) {
        dh = h2 - h1;
        if (dh > 180) dh -= 360;
        else if (dh < -180) dh += 360;
    }
    double dH = 2 * sqrt(c1p * c2p) * sin(dh / 2 * deg);
    double lBar = (lab1[0] + lab2[0]) / 2, cBarP = (c1p + c2p) / 2, hBar = h1 + h2;
    if (c1p * c2p != 0) {
        if (fabs(h1 - h2) <= 180) hBar = (h1 + h2) / 2;
        else hBar = (h1 + h2 < 360 ? h1 + h2 + 360 : h1 + h2 - 360) / 2;
    }
    double t = 1 - 0.17 * cos((hBar - 30) * deg) + 0.24 * cos(2 * hBar * deg) +
               0.32 * cos((3 * hBar + 6) * deg) - 0.20 * cos((4 * hBar - 63) * deg);
    double sl = 1 + 0.015 * (lBar - 50) * (lBar - 50) / sqrt(20 + (lBar - 50) * (lBar - 50));
    double sc = 1 + 0.045 * cBarP, sh = 1 + 0.015 * cBarP * t;
    double cBarP7 = pow(cBarP, 7);
    double rt = -2 * sqrt(cBarP7 / (cBarP7 + 6103515625.0)) *
                sin(60 * exp(-pow((hBar - 275) / 25, 2)) * deg);
    double l = dL / sl, c = dC / sc, h = dH / sh;
    return sqrt(l * l + c * c + h * h + rt * c * h);
}

// Closest palette entry by CIEDE2000.
String nearestStockColor(uint32_t color, const char *const *palette) {
    double source[3];
    toLab(color, source);
    const char *best = palette[0];
    double bestDistance = 1e18;
    for (size_t n = 0; n < PALETTE_SIZE; n++) {
        double lab[3];
        toLab(strtol(palette[n] + 1, nullptr, 16), lab);
        double d = deltaE2000(source, lab);
        if (d < bestDistance) {
            bestDistance = d;
            best = palette[n];
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
//
// zmod_nfc registers Klipper endpoints (zmod_nfc/scan, ...), which Moonraker
// publishes on its HTTP API; no extra port on the printer.

HttpResult moonraker(const PrinterConfig &printer, const String &path, const char *method,
                     const String &body = "", uint32_t timeout = TIMEOUT) {
    return request(printer.baseUrl() + path, method, body, printer.apiKey, timeout);
}

String moonrakerFailure(const HttpResult &result, const JsonDocument &doc) {
    if (result.code == 401 || result.code == 403)
        return "Moonraker refused the request: add this reader to [authorization] trusted_clients, "
               "or enter a Moonraker API key";
    return describeFailure(result, doc);
}

const char *const NO_ZMOD_NFC = "zmod_nfc is not installed on the printer, or too old for network readers";

String scanBody(const Tag &tag, const String &readerName, int slot) {
    JsonDocument doc;
    doc["reader"] = readerName;
    if (slot > 0) doc["slot"] = slot;
    doc["uid"] = tag.uidHex();
    doc["kind"] = tagKindName(tag.kind);
    if (tag.kind == TagKind::Type5) {
        doc["block_size"] = tag.blockSize;
    } else {
        doc["sak"] = tag.sak;
        doc["atqa"] = toHex(tag.atqa, 2);
    }
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

void describeOutcome(PrinterReply &reply, JsonObjectConst remote) {
    reply.ok = true;
    reply.outcome = remote["outcome"] | "";
    reply.title = remote["scan"]["title"] | "";
    reply.spoolId = remote["scan"]["spool_id"] | 0;
    String spoolman = remote["spoolman"] | "";
    if (reply.outcome == "prompt")
        reply.message = "Pick the slot on the printer's screen (or below)";
    else if (reply.outcome == "assigned")
        reply.message = remote["message"] | "Assigned to the printer's only slot";
    else if (reply.outcome == "no_slot")
        reply.ok = false, reply.message = remote["message"] | "The printer has no such slot";
    else if (reply.outcome == "disabled")
        reply.ok = false, reply.message = "zmod_nfc is disabled on the printer (NFC_ENABLE turns it on)";
    else if (reply.outcome == "printing")
        reply.ok = false, reply.message = "The printer is printing; zmod_nfc ignores tags while printing";
    else if (reply.outcome == "no_data")
        reply.ok = false, reply.message = "The printer found no filament data on this tag";
    else if (reply.outcome == "incomplete")
        reply.ok = false, reply.message = "The tag left the reader too soon; hold it still and scan again";
    else if (reply.outcome == "error")
        reply.ok = false, reply.message = String("zmod_nfc: ") + (remote["error"] | "scan failed");
    else
        reply.message = reply.outcome;
    if (spoolman.length() && reply.ok) reply.message += " (Spoolman: " + spoolman + ")";
}

PrinterReply zmodNfcScan(const PrinterConfig &printer, const Tag &tag, const String &readerName, int slot) {
    PrinterReply reply;
    HttpResult result = moonraker(printer, "/printer/zmod_nfc/scan", "POST", scanBody(tag, readerName, slot));
    JsonDocument doc;
    deserializeJson(doc, result.body);
    if (!result.ok()) {
        reply.message = result.code == 404 ? String(NO_ZMOD_NFC) : moonrakerFailure(result, doc);
        return reply;
    }
    JsonObjectConst accepted = doc["result"];
    if (!(accepted["accepted"] | false)) {
        describeOutcome(reply, accepted);
        return reply;
    }
    // Decoding and the Spoolman lookup run in the background on the
    // printer; the outcome shows up in printer.zmod_nfc.remote.
    String id = accepted["id"] | "";
    uint32_t deadline = millis() + SCAN_TIMEOUT;
    while ((int32_t)(millis() - deadline) < 0) {
        delay(300);
        HttpResult poll = moonraker(printer, "/printer/objects/query?zmod_nfc=remote", "GET");
        JsonDocument status;
        if (!poll.ok() || deserializeJson(status, poll.body)) continue;
        JsonObjectConst remote = status["result"]["status"]["zmod_nfc"]["remote"];
        if (id == (remote["id"] | "")) {
            describeOutcome(reply, remote);
            return reply;
        }
    }
    reply.message = "The printer took the tag but did not report back in time";
    return reply;
}

PrinterReply zmodNfcAssign(const PrinterConfig &printer, const Tag &tag, int slot, const String &readerName) {
    PrinterReply reply;
    String body = "{\"slot\":" + String(slot) + "}";
    for (int attempt = 0; attempt < 2; attempt++) {
        HttpResult result = moonraker(printer, "/printer/zmod_nfc/assign", "POST", body);
        JsonDocument doc;
        deserializeJson(doc, result.body);
        if (result.ok()) {
            reply.ok = true;
            reply.message = doc["result"]["message"] | "Assigned";
            reply.title = doc["result"]["scan"]["title"] | "";
            return reply;
        }
        reply.message = moonrakerFailure(result, doc);
        // The printer's prompt timed out or was dismissed: send the tag again.
        if (attempt == 0 && reply.message.indexOf("no scanned tag") >= 0) {
            PrinterReply scan = zmodNfcScan(printer, tag, readerName, 0);
            if (scan.ok && scan.outcome == "prompt") continue;
            return scan;
        }
        break;
    }
    return reply;
}

SlotList zmodNfcSlots(const PrinterConfig &printer) {
    SlotList list;
    HttpResult result = moonraker(printer, "/printer/zmod_nfc/info", "GET");
    JsonDocument doc;
    deserializeJson(doc, result.body);
    if (!result.ok()) {
        list.error = result.code == 404 ? String(NO_ZMOD_NFC) : moonrakerFailure(result, doc);
        return list;
    }
    for (JsonObjectConst s : doc["result"]["slots"].as<JsonArrayConst>()) {
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
    HttpResult result = moonraker(printer, "/printer/objects/query?zmod_color", "GET");
    JsonDocument doc;
    if (deserializeJson(doc, result.body) || !result.ok()) {
        error = moonrakerFailure(result, doc);
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
    HttpResult current = moonraker(printer, "/server/database/item?namespace=lane_data", "GET");
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
    HttpResult result = moonraker(printer, "/server/database/item", "POST", body);
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
            notes += "#" + color + " stored as palette color #" + snapped + ". ";
            color = snapped;
        }
    }
    JsonDocument cmd;
    cmd["script"] = "CHANGE_ZCOLOR SLOT=" + String(slot) + " HEX=" + color + " TYPE=" + material + " SILENT=1";
    String body;
    serializeJson(cmd, body);
    HttpResult result = moonraker(printer, "/printer/gcode/script", "POST", body, 15000);
    if (!result.ok()) {
        JsonDocument doc;
        deserializeJson(doc, result.body);
        reply.message = moonrakerFailure(result, doc);
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
    String material;
    bool c5 = printer.model == Model::Creator5;
    if (c5) {
        material = c5Material(f);
        if (!material.length()) {
            reply.message = f.material + " is not a Creator 5 material";
            return reply;
        }
    } else {
        // The AD5X shows any material name.
        material = upper(f.material.length() ? f.material : "PLA");
    }
    uint32_t color = f.color >= 0 ? f.color : 0xFFFFFF;
    String rgb = nearestStockColor(color, c5 ? C5_PALETTE : AD5X_PALETTE);
    JsonDocument body, response;
    stockAuth(printer, body);
    body["payload"]["cmd"] = "msConfig_cmd";
    JsonObject args = body["payload"]["args"].to<JsonObject>();
    args["slot"] = slot;
    args["mt"] = material;
    args["rgb"] = rgb;
    if (!stockCall(printer, "/control", body, response, reply.message)) return reply;
    reply.ok = true;
    reply.message = "Slot " + String(slot) + " is now " + material + " " + rgb;
    if (f.color >= 0 && rgb.substring(1) != f.colorHex())
        reply.message += " (the screen's closest color to #" + f.colorHex() + ")";
    return reply;
}

// --- Snapmaker U1 (paxx12's Extended firmware) ----------------------------------
//
// On the Extended firmware OpenRFID hands each head's tag reads to Klipper's
// filament_detect/set endpoint; this reader does the same for its head, so
// the screen, the slicer and SpoolLink treat the scan as the head's own read.
// SpoolLink (when Spoolman is set up) then looks the card UID up in Spoolman
// by itself and, if found, replaces the head's filament with the spool's.

constexpr int U1_HEADS = 4;
// How long SpoolLink gets to find the spool in Spoolman.
constexpr uint32_t SPOOLLINK_WAIT = 4000;

bool u1Status(const PrinterConfig &printer, JsonDocument &status, String &error, bool sensors = false) {
    String path = "/printer/objects/query?print_stats=state&filament_detect=state&print_task_config="
                  "filament_vendor,filament_type,filament_sub_type,filament_color_rgba,filament_spool_id";
    if (sensors)
        for (int n = 0; n < U1_HEADS; n++) path += "&filament_motion_sensor%20e" + String(n) + "_filament";
    HttpResult result = moonraker(printer, path, "GET");
    JsonDocument doc;
    if (deserializeJson(doc, result.body) || !result.ok()) {
        error = moonrakerFailure(result, doc);
        return false;
    }
    status.set(doc["result"]["status"]);
    if (status["filament_detect"].isNull() || status["print_task_config"].isNull()) {
        error = "No Snapmaker filament_detect on this printer (is it a Snapmaker U1?)";
        return false;
    }
    return true;
}

bool u1HasSpoolLink(const PrinterConfig &printer) {
    HttpResult result = moonraker(printer, "/server/info", "GET");
    JsonDocument doc;
    if (!result.ok() || deserializeJson(doc, result.body)) return false;
    for (JsonVariantConst c : doc["result"]["components"].as<JsonArrayConst>())
        if (c.as<String>() == "spoollink") return true;
    return false;
}

// What a head holds, from print_task_config: "Sunlu PLA Matte" and its color.
SlotInfo u1Head(const JsonDocument &status, int head) {
    JsonObjectConst ptc = status["print_task_config"];
    SlotInfo info;
    info.slot = head;
    info.label = "Head " + String(head);
    String vendor = ptc["filament_vendor"][head - 1] | "NONE";
    String type = ptc["filament_type"][head - 1] | "NONE";
    String sub = ptc["filament_sub_type"][head - 1] | "";
    if (type.length() && type != "NONE") {
        info.material = (vendor != "NONE" && vendor != "Generic" ? vendor + " " : String()) + type;
        if (sub.length() && sub != "NONE") info.material += " " + sub;
        String rgba = ptc["filament_color_rgba"][head - 1] | "";
        info.color = rgba.substring(0, 6);
    }
    return info;
}

SlotList u1Slots(const PrinterConfig &printer) {
    SlotList list;
    JsonDocument status;
    if (!u1Status(printer, status, list.error, true)) return list;
    for (int head = 1; head <= U1_HEADS; head++) {
        SlotInfo info = u1Head(status, head);
        String sensor = "filament_motion_sensor e" + String(head - 1) + "_filament";
        info.loaded = status[sensor]["filament_detected"] | false;
        list.slots.push_back(info);
    }
    list.ok = true;
    return list;
}

const char *u1CardType(TagKind kind) {
    switch (kind) {
    case TagKind::Type2: return "NTAG";
    case TagKind::MifareClassic: return "M1";
    default: return tagKindName(kind);
    }
}

PrinterReply u1Assign(const PrinterConfig &printer, const Tag &tag, const Filament &f, int head) {
    PrinterReply reply;
    if (head < 1 || head > U1_HEADS) {
        reply.message = "The U1 has heads 1 to " + String(U1_HEADS);
        return reply;
    }
    JsonDocument status;
    if (!u1Status(printer, status, reply.message)) return reply;
    String state = status["print_stats"]["state"] | "";
    if (state == "printing" || state == "paused") {
        reply.message = "The printer is printing; heads are not changed while it prints";
        return reply;
    }
    bool spoolLink = u1HasSpoolLink(printer);
    int spoolId = f.spoolId > 0 && f.spoolId < MAX_SPOOLMAN_ID ? f.spoolId : 0;
    // Without filament data only Spoolman can tell what the spool is.
    if (!f.hasFilamentData() && !spoolLink) {
        reply.message = spoolId ? "The tag only names Spoolman spool " + String(spoolId) +
                                      "; turn on Spoolman in the Extended firmware's settings to look it up"
                                : "No filament data on this tag, and Spoolman is not set up on the printer";
        return reply;
    }

    // As OpenRFID sends a read: brand "Generic" and no subtype unless it is
    // Snapmaker's own filament, so the slicer finds a matching profile.
    JsonDocument body;
    body["channel"] = head - 1;
    JsonObject info = body["info"].to<JsonObject>();
    JsonArray uid = info["CARD_UID"].to<JsonArray>();
    for (int n = 0; n < tag.uidLen; n++) uid.add(tag.uid[n]);
    info["CARD_TYPE"] = u1CardType(tag.kind);
    if (f.hasFilamentData()) {
        bool snapmaker = f.brand.equalsIgnoreCase("Snapmaker");
        info["VENDOR"] = snapmaker ? "Snapmaker" : "Generic";
        info["MAIN_TYPE"] = upper(f.material.length() ? f.material : String("PLA"));
        info["SUB_TYPE"] = snapmaker ? f.modifiers : String();
        if (f.nozzleMin) info["HOTEND_MIN_TEMP"] = f.nozzleMin;
        if (f.nozzleMax) info["HOTEND_MAX_TEMP"] = f.nozzleMax;
        if (f.bedTemp) info["BED_TEMP"] = f.bedTemp;
        info["ALPHA"] = f.alpha;
        info["RGB_1"] = f.color >= 0 ? f.color : 0xFFFFFF;
    }
    String text;
    serializeJson(body, text);
    HttpResult result = moonraker(printer, "/printer/filament_detect/set", "POST", text);
    if (!result.ok()) {
        JsonDocument doc;
        deserializeJson(doc, result.body);
        reply.message = result.code == 404 ? String("No filament_detect/set endpoint: this needs the Extended firmware")
                                           : moonrakerFailure(result, doc);
        return reply;
    }

    String notes;
    if (spoolId && spoolLink) {
        // Binds the card to the tag's spool in Spoolman, then loads that spool.
        JsonDocument cmd;
        cmd["script"] = "SET_SPOOL_ID CHANNEL=" + String(head - 1) + " SPOOL_ID=" + String(spoolId);
        String script;
        serializeJson(cmd, script);
        HttpResult set = moonraker(printer, "/printer/gcode/script", "POST", script, 15000);
        if (!set.ok()) notes = " Spoolman spool " + String(spoolId) + " was not set (" + String(set.code) + ").";
    }
    String error;
    if (spoolLink) {
        uint32_t deadline = millis() + SPOOLLINK_WAIT;
        do {
            delay(400);
            if (!u1Status(printer, status, error)) break;
        } while ((status["print_task_config"]["filament_spool_id"][head - 1] | 0) == 0 &&
                 (int32_t)(millis() - deadline) < 0);
    } else {
        u1Status(printer, status, error);
    }
    SlotInfo now = u1Head(status, head);
    int spool = status["print_task_config"]["filament_spool_id"][head - 1] | 0;
    if (!f.hasFilamentData() && !spool) {
        reply.message = "SpoolLink found no Spoolman spool for this tag, and the tag has no filament data";
        return reply;
    }
    reply.ok = true;
    reply.title = now.material;
    reply.spoolId = spool;
    reply.message = now.label + " is now " + now.material + " #" + now.color;
    if (spool) reply.message += " (Spoolman spool " + String(spool) + ")";
    else if (spoolLink) reply.message += " (no Spoolman spool for this tag)";
    reply.message += notes;
    return reply;
}

}  // namespace

PrinterReply sendScan(const PrinterConfig &printer, const Tag &tag, const String &readerName, int slot) {
    if (printer.method == Method::ZmodNfc) return zmodNfcScan(printer, tag, readerName, slot);
    PrinterReply reply;
    reply.message = "only the zmod_nfc method takes whole scans";
    return reply;
}

PrinterReply assignSlot(const PrinterConfig &printer, const Tag &tag, const Filament &filament, int slot,
                        const String &readerName) {
    switch (printer.method) {
    case Method::ZmodNfc: return zmodNfcAssign(printer, tag, slot, readerName);
    case Method::ZmodBasic: return basicAssign(printer, tag, filament, slot);
    case Method::Snapmaker: return u1Assign(printer, tag, filament, slot);
    default: return stockAssign(printer, filament, slot);
    }
}

void cancelPrompt(const PrinterConfig &printer) {
    if (printer.method == Method::ZmodNfc) moonraker(printer, "/printer/zmod_nfc/cancel", "POST", "{}");
}

SlotList listSlots(const PrinterConfig &printer) {
    switch (printer.method) {
    case Method::ZmodNfc: return zmodNfcSlots(printer);
    case Method::ZmodBasic: return basicSlots(printer);
    case Method::Snapmaker: return u1Slots(printer);
    default: return stockSlots(printer);
    }
}

PrinterReply testPrinter(const PrinterConfig &printer) {
    PrinterReply reply;
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
