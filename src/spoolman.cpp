#include "spoolman.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <mbedtls/base64.h>

namespace {

constexpr uint32_t TIMEOUT = 10000;
// A scan report waits for no longer: the printer gets the scan after it.
constexpr uint32_t REPORT_TIMEOUT = 2500;
// Spoolman's limit for a scan's base64 payload (8192 characters).
constexpr size_t MAX_PAYLOAD = 6144;
// Larger "spool IDs" on tags are not Spoolman's (phone apps write
// timestamps there).
constexpr int MAX_SPOOLMAN_ID = 10000000;
const char *const CARD_UIDS = "card_uids";
const char *const NFC_ID = "nfc_id";

// One Spoolman API call; the reply goes into doc, through filter when
// given (the spool list can be far larger than the reply we need).
bool call(const String &base, const String &apiKey, const char *method, const String &path, const String &body,
          JsonDocument &doc, String &error, const JsonDocument *filter = nullptr, uint32_t timeout = TIMEOUT) {
    if (WiFi.status() != WL_CONNECTED) {
        error = "Wi-Fi is not connected";
        return false;
    }
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(min(timeout, (uint32_t)3000));
    http.setTimeout(timeout);
    // HTTP/1.0: the reply is never chunked, so it can be parsed off the stream.
    http.useHTTP10(true);
    if (!http.begin(client, base + "/api/v1" + path)) {
        error = "bad address " + base;
        return false;
    }
    if (body.length()) http.addHeader("Content-Type", "application/json");
    if (apiKey.length()) http.addHeader("X-Api-Key", apiKey);
    int code = http.sendRequest(method, (uint8_t *)body.c_str(), body.length());
    if (code <= 0) {
        error = "Spoolman not reachable: " + HTTPClient::errorToString(code);
        http.end();
        return false;
    }
    DeserializationError parsed = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                         : deserializeJson(doc, http.getStream());
    http.end();
    if (code < 200 || code >= 300) {
        // Spoolman's own errors carry "message"; request checks "detail".
        const char *message = doc["message"] | (doc["detail"].is<const char *>() ? doc["detail"].as<const char *>() : nullptr);
        error = message ? "Spoolman: " + String(message) : "Spoolman answered HTTP " + String(code);
        return false;
    }
    // Empty replies (204) are fine.
    if (parsed && parsed != DeserializationError::EmptyInput) {
        error = "Spoolman's reply was not readable (" + String(parsed.c_str()) + ")";
        return false;
    }
    return true;
}

bool call(const PrinterConfig &server, const char *method, const String &path, const String &body,
          JsonDocument &doc, String &error, const JsonDocument *filter = nullptr) {
    return call(server.baseUrl(), server.apiKey, method, path, body, doc, error, filter);
}

String json(const JsonDocument &doc) {
    String text;
    serializeJson(doc, text);
    return text;
}

String urlEncode(const String &text) {
    String out;
    for (char c : text) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
            out += hex;
        }
    }
    return out;
}

String normalizeUid(String uid) {
    String out;
    for (char c : uid) {
        if (c == ':' || c == ' ' || c == '-') continue;
        if (!isxdigit((unsigned char)c)) return "";
        out += (char)toupper((unsigned char)c);
    }
    return out;
}

// Extra fields hold JSON: a text field's value is "\"04A1B2C3\"".
String extraText(JsonVariantConst spool, const char *field) {
    String raw = spool["extra"][field] | "";
    raw.trim();
    if (raw.length() >= 2 && raw[0] == '"' && raw[raw.length() - 1] == '"') raw = raw.substring(1, raw.length() - 1);
    return raw;
}

std::vector<String> cardUids(JsonVariantConst spool) {
    std::vector<String> uids;
    String text = extraText(spool, CARD_UIDS);
    int start = 0;
    while (start <= (int)text.length()) {
        int comma = text.indexOf(',', start);
        if (comma < 0) comma = text.length();
        String uid = normalizeUid(text.substring(start, comma));
        if (uid.length()) uids.push_back(uid);
        start = comma + 1;
    }
    return uids;
}

bool hasUid(JsonVariantConst spool, const String &uid) {
    for (auto &u : cardUids(spool))
        if (u == uid) return true;
    return normalizeUid(extraText(spool, NFC_ID)) == uid;
}

String spoolColor(JsonVariantConst spool) {
    String colors = String(spool["filament"]["color_hex"] | "") + "," + String(spool["filament"]["multi_color_hexes"] | "");
    int start = 0;
    while (start < (int)colors.length()) {
        int comma = colors.indexOf(',', start);
        if (comma < 0) comma = colors.length();
        String color = colors.substring(start, comma);
        color.replace("#", "");
        color.trim();
        color.toUpperCase();
        bool hex = color.length() >= 6;
        for (int i = 0; hex && i < 6; i++) hex = isxdigit((unsigned char)color[i]);
        if (hex) return color.substring(0, 6);
        start = comma + 1;
    }
    return "";
}

// Vendor, material and name, each word once: filament names often repeat
// the vendor or material ("PLA SnapSpeed").
String spoolTitle(JsonVariantConst spool) {
    JsonVariantConst filament = spool["filament"];
    String parts[3] = {filament["vendor"]["name"] | "", filament["material"] | "", filament["name"] | ""};
    String title, lowered;
    for (auto &part : parts) {
        int start = 0;
        while (start < (int)part.length()) {
            int space = part.indexOf(' ', start);
            if (space < 0) space = part.length();
            String word = part.substring(start, space);
            String low = word;
            low.toLowerCase();
            if (word.length() && (" " + lowered + " ").indexOf(" " + low + " ") < 0) {
                title += (title.length() ? " " : "") + word;
                lowered += (lowered.length() ? " " : "") + low;
            }
            start = space + 1;
        }
    }
    return title.length() ? title : "spool " + String(spool["id"] | 0);
}

float density(const String &material) {
    String base = material;
    base.toUpperCase();
    int dash = base.indexOf('-');
    if (dash > 0) base = base.substring(0, dash);
    static const struct {
        const char *material;
        float density;
    } table[] = {{"PLA", 1.24}, {"PETG", 1.27}, {"ABS", 1.04}, {"ASA", 1.07}, {"TPU", 1.21}, {"PA", 1.14},
                 {"PC", 1.20},  {"PCTG", 1.23}, {"PVA", 1.23}, {"HIPS", 1.04}, {"PP", 0.90}};
    for (auto &row : table)
        if (base == row.material) return row.density;
    return 1.24;
}

// Spoolman's name for a tag's kind: the decoded format ("bambu",
// "openprinttag", ...), else the chip ("ntag" for type 2).
String tagFormat(const Tag &tag, const Filament &f) {
    if (f.valid() && f.format != "Text") {
        String format = f.format;
        format.toLowerCase();
        return format;
    }
    switch (tag.kind) {
    case TagKind::Type2: return "ntag";
    case TagKind::MifareClassic: return "mifare_classic";
    case TagKind::Type4: return "type4";
    case TagKind::Type5: return "iso15693";
    default: return "";
    }
}

// Spoolman's reader ids allow letters, digits and ._:- only.
String readerId(const String &readerName) {
    String id;
    for (char c : readerName) id += isalnum((unsigned char)c) || c == '.' || c == '_' || c == ':' || c == '-' ? c : '-';
    return id.substring(0, 64);
}

// Link the tag to a spool in Spoolman's own tag list.
bool linkTag(const PrinterConfig &server, int spoolId, const String &uid, const String &format, String &error) {
    JsonDocument body, reply;
    body["uid"] = uid;
    if (format.length()) body["format"] = format;
    return call(server, "POST", "/spool/" + String(spoolId) + "/tag", json(body), reply, error);
}

// Spoolman refuses extra fields it does not know: add "Card UIDs" once.
bool ensureCardField(const PrinterConfig &server, String &error) {
    JsonDocument fields;
    if (!call(server, "GET", "/field/spool", "", fields, error)) return false;
    for (JsonVariantConst f : fields.as<JsonArrayConst>())
        if (f["key"] == CARD_UIDS) return true;
    JsonDocument body, reply;
    body["name"] = "Card UIDs";
    body["field_type"] = "text";
    body["order"] = 1;
    body["default_value"] = "\"\"";
    return call(server, "POST", "/field/spool/" + String(CARD_UIDS), json(body), reply, error);
}

bool setCardUids(const PrinterConfig &server, int spoolId, const std::vector<String> &uids, String &error) {
    String joined;
    for (auto &u : uids) joined += (joined.length() ? "," : "") + u;
    JsonDocument body, reply;
    body["extra"][CARD_UIDS] = "\"" + joined + "\"";
    return call(server, "PATCH", "/spool/" + String(spoolId), json(body), reply, error);
}

// Add uid to a spool's Card UIDs, taking it off any other spool (a tag
// belongs to one spool).
bool bindUid(const PrinterConfig &server, JsonArrayConst spools, JsonVariantConst spool, const String &uid,
             String &error) {
    if (!ensureCardField(server, error)) return false;
    int id = spool["id"] | 0;
    for (JsonVariantConst other : spools) {
        if ((other["id"] | 0) == id) continue;
        std::vector<String> uids = cardUids(other);
        size_t before = uids.size();
        for (size_t i = uids.size(); i-- > 0;)
            if (uids[i] == uid) uids.erase(uids.begin() + i);
        if (uids.size() != before && !setCardUids(server, other["id"] | 0, uids, error)) return false;
    }
    std::vector<String> uids = cardUids(spool);
    uids.push_back(uid);
    return setCardUids(server, id, uids, error);
}

// Vendor, filament and spool for a decoded tag, reusing a vendor and a
// filament Spoolman already has with the same name, material and color.
bool createSpool(const PrinterConfig &server, const Filament &f, const String &uid, JsonDocument &spool,
                 String &error) {
    int vendorId = 0;
    if (f.brand.length()) {
        JsonDocument vendors;
        if (!call(server, "GET", "/vendor?name=" + urlEncode(f.brand), "", vendors, error)) return false;
        for (JsonVariantConst v : vendors.as<JsonArrayConst>())
            if (f.brand.equalsIgnoreCase(v["name"] | "")) vendorId = v["id"] | 0;
        if (!vendorId) {
            JsonDocument body, vendor;
            body["name"] = f.brand;
            if (!call(server, "POST", "/vendor", json(body), vendor, error)) return false;
            vendorId = vendor["id"] | 0;
        }
    }
    String name = f.name;
    if (!name.length()) {
        name = f.material + " " + f.modifiers;
        name.trim();
    }
    if (!name.length()) name = "Unnamed";
    String color = f.colorHex();
    String query;
    if (f.material.length()) query += "&material=" + urlEncode(f.material);
    if (vendorId) query += "&vendor.id=" + String(vendorId);
    if (color.length()) query += "&color_hex=" + color;
    JsonDocument filter, filaments;
    JsonObject only = filter.add<JsonObject>();
    only["id"] = true;
    only["name"] = true;
    only["material"] = true;
    only["color_hex"] = true;
    if (!call(server, "GET", "/filament?" + query.substring(query.length() ? 1 : 0), "", filaments, error, &filter))
        return false;
    // Spoolman's filters are loose (material "PLA" also finds "PLA+", a
    // color also finds similar ones): reuse only an exact match.
    int filamentId = 0;
    for (JsonVariantConst existing : filaments.as<JsonArrayConst>()) {
        String existingColor = existing["color_hex"] | "";
        if (name == (existing["name"] | "") && f.material.equalsIgnoreCase(existing["material"] | "") &&
            color.equalsIgnoreCase(existingColor.substring(0, 6)))
            filamentId = existing["id"] | 0;
    }
    if (!filamentId) {
        JsonDocument body, filament;
        body["name"] = name;
        if (f.material.length()) body["material"] = f.material;
        body["density"] = density(f.material);
        body["diameter"] = f.diameter > 0 ? f.diameter : 1.75f;
        if (vendorId) body["vendor_id"] = vendorId;
        if (color.length()) body["color_hex"] = color;
        if (f.weight) body["weight"] = f.weight;
        if (f.nozzleTemp()) body["settings_extruder_temp"] = f.nozzleTemp();
        if (f.bedTemp) body["settings_bed_temp"] = f.bedTemp;
        if (!call(server, "POST", "/filament", json(body), filament, error)) return false;
        filamentId = filament["id"] | 0;
    }
    if (!ensureCardField(server, error)) return false;
    JsonDocument body;
    body["filament_id"] = filamentId;
    if (f.weight) body["initial_weight"] = f.weight;
    if (server.location.length()) body["location"] = server.location;
    body["extra"][CARD_UIDS] = "\"" + uid + "\"";
    return call(server, "POST", "/spool", json(body), spool, error);
}

}  // namespace

PrinterReply spoolmanScan(const PrinterConfig &server, const Tag &tag, const Filament &f, const String &readerName) {
    PrinterReply reply;
    String uid = normalizeUid(tag.uidHex());
    JsonDocument spool;     // the spool the tag belongs to, as Spoolman has it
    String how;
    // Spoolman's own tag links first (and its web page hears of the scan).
    TagMatch linked = reportScan(server.baseUrl(), server.apiKey, tag, f, readerName);
    int tagSpool = f.spoolId > 0 && f.spoolId < MAX_SPOOLMAN_ID ? f.spoolId : 0;
    if (tagSpool) {
        if (!call(server, "GET", "/spool/" + String(tagSpool), "", spool, reply.message)) return reply;
        how = "named on the tag";
    } else if (linked.spoolId) {
        if (!call(server, "GET", "/spool/" + String(linked.spoolId), "", spool, reply.message)) return reply;
        how = "found by its tag";
    } else if (linked.filamentId) {
        // A filament's tag (a sample card, say) names no one spool.
        reply.ok = true;
        reply.title = f.title();
        reply.message = "Tag linked to Spoolman filament " + String(linked.filamentId) + ", not to a spool";
        return reply;
    } else {
        JsonDocument filter, spools;
        JsonObject only = filter.add<JsonObject>();
        only["id"] = true;
        only["archived"] = true;
        only["location"] = true;
        only["remaining_weight"] = true;
        only["extra"][CARD_UIDS] = true;
        only["extra"][NFC_ID] = true;
        JsonObject filament = only["filament"].to<JsonObject>();
        filament["name"] = true;
        filament["material"] = true;
        filament["color_hex"] = true;
        filament["multi_color_hexes"] = true;
        filament["vendor"]["name"] = true;
        // Archived spools too: a tag moved to a new spool leaves its old one.
        if (!call(server, "GET", "/spool?allow_archived=true", "", spools, reply.message, &filter)) return reply;
        JsonArrayConst list = spools.as<JsonArrayConst>();
        JsonVariantConst found;
        for (JsonVariantConst s : list)
            if (!(s["archived"] | false) && uid.length() && hasUid(s, uid)) {
                found = s;
                how = "found by its tag";
                break;
            }
        // A spool's second tag, or a tag never bound: the one spool with the
        // tag's vendor, material and color.
        if (found.isNull() && f.brand.length() && f.material.length() && f.color >= 0) {
            String color = f.colorHex();
            std::vector<JsonVariantConst> matches;
            for (JsonVariantConst s : list)
                if (!(s["archived"] | false) && f.brand.equalsIgnoreCase(s["filament"]["vendor"]["name"] | "") &&
                    f.material.equalsIgnoreCase(s["filament"]["material"] | "") && spoolColor(s) == color)
                    matches.push_back(s);
            if (matches.size() > 1) {
                String ids;
                for (auto &m : matches) ids += (ids.length() ? ", #" : "#") + String(m["id"] | 0);
                reply.title = f.title();
                reply.message = String(matches.size()) + " spools match (" + ids + "); add this tag's UID " + uid +
                                " to the right one's Card UIDs in Spoolman";
                return reply;
            }
            if (matches.size() == 1 && uid.length()) {
                if (!bindUid(server, list, matches[0], uid, reply.message)) return reply;
                found = matches[0];
                how = "same filament; tag bound to it";
            }
        }
        if (!found.isNull()) {
            spool.set(found);
        } else if (!f.hasFilamentData()) {
            reply.message = "Tag " + uid + " is not in Spoolman, and has no filament data to add it with";
            return reply;
        } else if (!server.spoolmanCreate) {
            reply.title = f.title();
            reply.message = "Not in Spoolman (tag " + uid + "); pick \"Add them as new spools\" to add it";
            return reply;
        } else {
            if (!createSpool(server, f, uid, spool, reply.message)) return reply;
            how = "added";
        }
    }
    int id = spool["id"] | 0;
    // Into Spoolman's own tag list as well, unless it has the tag already
    // (then it said so above), and the scan once more so the page shows
    // this spool.
    if (linked.ok && !linked.spoolId && !linked.filamentId && uid.length()) {
        String error;
        if (linkTag(server, id, uid, tagFormat(tag, f), error))
            reportScan(server.baseUrl(), server.apiKey, tag, f, readerName);
        else
            log_w("tag %s not linked to spool %d: %s", uid.c_str(), id, error.c_str());
    }
    reply.ok = true;
    reply.spoolId = id;
    reply.title = spoolTitle(spool);
    reply.message = "Spoolman spool " + String(id) + ", " + how;
    if (!spool["remaining_weight"].isNull()) reply.message += "; " + String(lround(spool["remaining_weight"].as<float>())) + " g left";
    String location = spool["location"] | "";
    if (server.location.length() && location != server.location) {
        JsonDocument body, patched;
        body["location"] = server.location;
        String error;
        if (call(server, "PATCH", "/spool/" + String(id), json(body), patched, error))
            reply.message += "; now in " + server.location;
        else
            reply.message += "; location not set: " + error;
    }
    return reply;
}

String spoolmanBase(String address) {
    address.trim();
    if (!address.length()) return "";
    if (address.startsWith("http://")) address = address.substring(7);
    int slash = address.indexOf('/');
    if (slash >= 0) address = address.substring(0, slash);
    if (address.indexOf(':') < 0) address += ":" + String(SPOOLMAN_PORT);
    return "http://" + address;
}

TagMatch reportScan(const String &base, const String &apiKey, const Tag &tag, const Filament &f,
                    const String &readerName) {
    TagMatch match;
    String uid = normalizeUid(tag.uidHex());
    if (!base.length() || !uid.length()) return match;
    JsonDocument body, reply;
    body["uid"] = uid;
    body["reader_id"] = readerId(readerName);
    body["name"] = readerName.substring(0, 64);
    String format = tagFormat(tag, f);
    if (format.length()) body["format"] = format;
    const std::vector<uint8_t> &raw = tag.kind == TagKind::Type4 ? tag.ndef : tag.memory;
    if (!raw.empty() && raw.size() <= MAX_PAYLOAD) {
        size_t len = 0;
        std::vector<uint8_t> b64(4 * ((raw.size() + 2) / 3) + 1);
        if (!mbedtls_base64_encode(b64.data(), b64.size(), &len, raw.data(), raw.size()))
            body["payload_b64"] = String((const char *)b64.data(), len);
    }
    if (!call(base, apiKey, "POST", "/tag/scan", json(body), reply, match.error, nullptr, REPORT_TIMEOUT)) {
        log_w("scan not reported to Spoolman: %s", match.error.c_str());
        return match;
    }
    match.ok = true;
    match.spoolId = reply["matched_spool_id"] | 0;
    match.filamentId = reply["matched_filament_id"] | 0;
    return match;
}

bool spoolForUid(const String &base, const String &apiKey, const String &tagUid, int &spoolId, String &error) {
    spoolId = 0;
    String uid = normalizeUid(tagUid);
    if (!uid.length()) return true;
    JsonDocument filter;
    JsonObject only = filter.add<JsonObject>();
    only["id"] = true;
    only["archived"] = true;
    only["tags"][0]["uid"] = true;
    only["extra"][CARD_UIDS] = true;
    only["extra"][NFC_ID] = true;
    // Spoolman 0.27 finds the spool of its own tag links; older versions
    // ignore "tag" and list every spool, which the check covers. Card UIDs
    // are looked for in the whole list.
    for (const String &path : std::vector<String>{"/spool?tag=" + uid, "/spool"}) {
        JsonDocument spools;
        if (!call(base, apiKey, "GET", path, "", spools, error, &filter)) return false;
        JsonArrayConst list = spools.as<JsonArrayConst>();
        for (JsonVariantConst s : list) {
            if (s["archived"] | false) continue;
            bool linked = hasUid(s, uid);
            for (JsonVariantConst t : s["tags"].as<JsonArrayConst>())
                if (normalizeUid(t["uid"] | "") == uid) linked = true;
            if (linked) {
                spoolId = s["id"] | 0;
                return true;
            }
        }
        if (list.size() > 1) break;
    }
    return true;
}

SlotList spoolmanStatus(const PrinterConfig &server) {
    SlotList list;
    JsonDocument info;
    if (!call(server, "GET", "/info", "", info, list.error)) return list;
    String version = info["version"] | "";
    if (!version.length()) {
        list.error = "this is not Spoolman (no version in /api/v1/info)";
        return list;
    }
    SlotInfo slot;
    slot.slot = 1;
    slot.label = "Spoolman";
    slot.material = version;
    slot.loaded = true;
    list.slots.push_back(slot);
    list.note = "Connected to Spoolman " + version;
    list.ok = true;
    return list;
}
