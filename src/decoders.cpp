#include "decoders.h"

#include "keys.h"
#include "ndef.h"
#include "openprinttag.h"

namespace {

typedef Filament (*Decoder)(const Tag &tag, const std::vector<NdefRecord> &records);

// --- helpers ---------------------------------------------------------------

String ascii(const uint8_t *data, size_t len) {
    String text;
    for (size_t i = 0; i < len && data[i]; i++)
        if (data[i] < 0x80) text += (char)data[i];
    text.trim();
    return text;
}

uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
uint16_t be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

// A JSON number or numeric string (OpenSpool apps write both), or 0.
int jsonInt(JsonVariantConst value) {
    if (value.is<const char *>()) return atoi(value.as<const char *>());
    if (value.is<float>()) return (int)value.as<float>();
    return 0;
}

int32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((int32_t)r << 16) | (g << 8) | b; }

bool openedWith(const Tag &tag, const char *name, int sector = 1) {
    return tag.kind == TagKind::MifareClassic && tag.sectorRead(sector) &&
           strcmp(tag.sectorKey[sector], name) == 0;
}

void addModifier(String &modifiers, const String &word) {
    if (!word.length()) return;
    if (modifiers.length()) modifiers += ' ';
    modifiers += word;
}

bool isDigits(const String &text) {
    if (!text.length()) return false;
    for (char c : text)
        if (!isdigit((unsigned char)c)) return false;
    return true;
}

// Bed temperature defaults by material, for formats that only carry
// nozzle temperatures (FF_NFC's MATERIAL_DEFAULTS).
Filament withDefaults(Filament f) {
    static const struct { const char *material; int bed; } DEFAULTS[] = {
        {"PLA", 55}, {"PLA-CF", 60}, {"PETG", 70}, {"PETG-CF", 70}, {"ABS", 95},
        {"ASA", 95}, {"TPU", 38}, {"PA", 110}, {"PA-CF", 110}, {"PC", 100},
        {"PCTG", 80}, {"PVA", 45}, {"HIPS", 100}, {"PP", 90}, {"BVOH", 60},
    };
    if (!f.bedTemp)
        for (auto &d : DEFAULTS)
            if (f.material == d.material) f.bedTemp = d.bed;
    return f;
}

String urlDecode(const String &text) {
    String out;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < text.length() && isxdigit((unsigned char)text[i + 1]) &&
                   isxdigit((unsigned char)text[i + 2])) {
            out += (char)strtol(text.substring(i + 1, i + 3).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

// Digits after the first occurrence of one of the markers, or 0.
int numberAfter(const String &text, const char *const *markers, size_t count) {
    String lower = text;
    lower.toLowerCase();
    for (size_t i = 0; i < count; i++) {
        int at = lower.indexOf(markers[i]);
        if (at < 0) continue;
        int pos = at + strlen(markers[i]);
        int value = 0;
        bool any = false;
        while (pos < (int)lower.length() && isdigit((unsigned char)lower[pos])) {
            value = value * 10 + (lower[pos++] - '0');
            any = true;
        }
        if (any) return value;
    }
    return 0;
}

// --- Spoolman references -------------------------------------------------

// nfc2klipper text records "SPOOL:23\nFILAMENT:14", Spoolman links
// "web+spoolman:s-23" or ".../spool/show/23".
Filament decodeSpoolman(const Tag &, const std::vector<NdefRecord> &records) {
    Filament f;
    for (auto &record : records) {
        if (record.isText()) {
            String text = record.text();
            int start = 0;
            while (start <= (int)text.length()) {
                int end = text.indexOf('\n', start);
                if (end < 0) end = text.length();
                String line = text.substring(start, end);
                int colon = line.indexOf(':');
                if (colon > 0) {
                    String key = line.substring(0, colon), value = line.substring(colon + 1);
                    key.trim();
                    key.toUpperCase();
                    value.trim();
                    if (key == "SPOOL" && isDigits(value)) f.spoolId = value.toInt();
                    if (key == "FILAMENT" && isDigits(value)) f.filamentId = value.toInt();
                }
                start = end + 1;
            }
        } else if (record.isUri()) {
            static const char *const SPOOL[] = {"web+spoolman:s-", "/spool/show/"};
            static const char *const FILAMENT[] = {"web+spoolman:f-", "/filament/show/"};
            String uri = record.uri();
            if (int id = numberAfter(uri, SPOOL, 2)) f.spoolId = id;
            if (int id = numberAfter(uri, FILAMENT, 2)) f.filamentId = id;
        }
    }
    if (f.spoolId || f.filamentId) f.format = "Spoolman";
    return f;
}

// --- OpenPrintTag (Prusa) --------------------------------------------------

Filament decodeOpenPrintTag(const Tag &, const std::vector<NdefRecord> &records) {
    for (auto &record : records) {
        if (record.mime() != "application/vnd.openprinttag") continue;
        OpenPrintTag opt;
        if (!parseOpenPrintTag(record.payload.data(), record.payload.size(), opt)) continue;
        Filament f;
        f.format = "OpenPrintTag";
        normalizeMaterial(opt.material.c_str(), f.material, f.modifiers);
        f.brand = opt.brand.c_str();
        f.name = opt.name.c_str();
        f.color = opt.color;
        f.alpha = opt.alpha;
        f.diameter = opt.diameter;
        f.weight = opt.weight;
        f.nozzleMin = opt.nozzleMin;
        f.nozzleMax = opt.nozzleMax;
        f.bedTemp = opt.bedTemp;
        return withDefaults(f);
    }
    return Filament();
}

// --- OpenSpool -------------------------------------------------------------

Filament decodeOpenSpool(const Tag &, const std::vector<NdefRecord> &records) {
    for (auto &record : records) {
        if (record.mime() != "application/json") continue;
        JsonDocument doc;
        if (deserializeJson(doc, record.payload.data(), record.payload.size())) continue;
        String protocol = doc["protocol"] | "";
        protocol.toLowerCase();
        if (protocol != "openspool") continue;
        Filament f;
        f.format = "OpenSpool";
        normalizeMaterial(doc["type"] | "PLA", f.material, f.modifiers);
        addModifier(f.modifiers, doc["subtype"] | "");
        f.brand = doc["brand"] | "";
        f.color = parseColor(doc["color_hex"] | "");
        JsonVariant alpha = doc["alpha"];
        if (alpha.is<const char *>())
            f.alpha = constrain(strtol(alpha.as<const char *>(), nullptr, 16), 0, 255);
        else if (alpha.is<int>())
            f.alpha = constrain(alpha.as<int>(), 0, 255);
        JsonVariantConst diameter = doc["diameter"];
        f.diameter = diameter.is<const char *>() ? atof(diameter.as<const char *>()) : (diameter | 1.75f);
        if (f.diameter <= 0) f.diameter = 1.75f;
        f.weight = jsonInt(doc["weight"]);
        f.nozzleMin = jsonInt(doc["min_temp"]);
        f.nozzleMax = jsonInt(doc["max_temp"]);
        f.bedTemp = jsonInt(doc["bed_max_temp"]);
        if (!f.bedTemp) f.bedTemp = jsonInt(doc["bed_min_temp"]);
        f.spoolId = jsonInt(doc["spool_id"]);
        return withDefaults(f);
    }
    return Filament();
}

// --- SpoolEase -------------------------------------------------------------

// URI records https://tag.spoolease.io/S1?M=PLA&CC=FF0000FF&...
Filament decodeSpoolEase(const Tag &, const std::vector<NdefRecord> &records) {
    for (auto &record : records) {
        if (!record.isUri()) continue;
        String uri = record.uri();
        String lower = uri;
        lower.toLowerCase();
        int at = lower.indexOf("://tag.spoolease.io/s1");
        int query = uri.indexOf('?');
        if (at < 0 || query < 0) continue;
        String material = "PLA", sub, brand, colors = "FFFFFFFF";
        int weight = 0, nmin = 0, nmax = 0;
        int pos = query + 1;
        while (pos < (int)uri.length()) {
            int end = uri.indexOf('&', pos);
            if (end < 0) end = uri.length();
            String part = uri.substring(pos, end);
            int eq = part.indexOf('=');
            String key = urlDecode(eq < 0 ? part : part.substring(0, eq));
            String value = eq < 0 ? "" : urlDecode(part.substring(eq + 1));
            if (value.length()) {
                if (key == "M") material = value;
                else if (key == "MS") sub = value;
                else if (key == "B") brand = value;
                else if (key == "CC") colors = value;
                else if (key == "WL") weight = value.toInt();
                else if (key == "NN") nmin = value.toInt();
                else if (key == "NX") nmax = value.toInt();
            }
            pos = end + 1;
        }
        material.toUpperCase();
        if (material == "ABS-S") material = "ABS";
        else if (material == "PA-S") material = "PA";
        else if (material == "PLA-S") material = "PLA";
        else if (material == "TPU-AMS") material = "TPU";
        Filament f;
        f.format = "SpoolEase";
        normalizeMaterial(material, f.material, f.modifiers);
        addModifier(f.modifiers, sub);
        f.brand = brand;
        String first = colors;
        int semi = first.indexOf(';');
        if (semi >= 0) first = first.substring(0, semi);
        first.trim();
        if (first.startsWith("#")) first = first.substring(1);
        f.color = parseColor(first);
        if (first.length() == 8) f.alpha = strtol(first.substring(6).c_str(), nullptr, 16);
        f.weight = weight;
        f.nozzleMin = nmin;
        f.nozzleMax = nmax;
        return withDefaults(f);
    }
    return Filament();
}

// --- Bambu Lab -------------------------------------------------------------

Filament decodeBambu(const Tag &tag, const std::vector<NdefRecord> &) {
    if (!openedWith(tag, "Bambu")) return Filament();
    const uint8_t *d = tag.memory.data();
    Filament f;
    f.format = "Bambu";
    String material = ascii(d + 32, 16), detailed = ascii(d + 64, 16);
    String modifier = detailed.startsWith(material) ? detailed.substring(material.length()) : detailed;
    modifier.trim();
    normalizeMaterial(material, f.material, f.modifiers);
    if (modifier.length() && modifier != material) addModifier(f.modifiers, modifier);
    f.brand = "Bambu Lab";
    f.presetId = ascii(d + 24, 8);
    f.color = rgb(d[80], d[81], d[82]);
    f.alpha = d[83];
    f.weight = le16(d + 84);
    float diameter;
    memcpy(&diameter, d + 88, 4);
    f.diameter = diameter > 0 && diameter < 5 ? diameter : 1.75f;
    f.bedTemp = le16(d + 102);
    f.nozzleMax = le16(d + 104);
    f.nozzleMin = le16(d + 106);
    return f;
}

// --- Creality --------------------------------------------------------------

struct CrealityMaterial { const char *code, *material, *modifiers; int nmin, nmax, bed; };
const CrealityMaterial CREALITY_MATERIALS[] = {
    {"01001", "PLA", "Hyper", 190, 240, 50}, {"02001", "PLA-CF", "Hyper", 190, 240, 50},
    {"06002", "PETG", "Hyper", 220, 270, 70}, {"03001", "ABS", "Hyper", 240, 280, 80},
    {"09002", "PLA", "Ender Fast", 190, 240, 50}, {"04001", "PLA", "CR", 190, 240, 50},
    {"05001", "PLA", "CR Silk", 190, 240, 50}, {"06001", "PETG", "CR", 220, 270, 70},
    {"07001", "ABS", "CR", 240, 280, 100}, {"08001", "PLA", "Ender", 190, 240, 50},
    {"09001", "PLA", "EN +", 190, 240, 50}, {"10001", "TPU", "HP", 190, 240, 40},
    {"11001", "PA", "CR Nylon", 250, 270, 50}, {"13001", "PLA", "CR Carbon", 190, 240, 50},
    {"14001", "PLA", "CR Matte", 190, 240, 50}, {"15001", "PLA", "CR Fluo", 190, 240, 50},
    {"16001", "TPU", "CR", 210, 240, 40}, {"17001", "PLA", "CR Wood", 190, 240, 50},
    {"18001", "PLA", "HP Ultra", 190, 240, 50}, {"19001", "ASA", "HP", 240, 280, 90},
    {"12003", "PA-CF", "Hyper PAHT", 280, 320, 90}, {"12002", "PA-CF", "Hyper PPA", 280, 320, 100},
    {"07002", "PC", "Hyper", 250, 270, 110}, {"01601", "PLA", "Soleyin Ultra", 190, 240, 50},
};
const char CREALITY_AES_KEY[] = "H@CFkRnz@KAtBJp2";

Filament decodeCreality(const Tag &tag, const std::vector<NdefRecord> &) {
    if (!openedWith(tag, "Creality")) return Filament();
    uint8_t data[48];
    memcpy(data, tag.memory.data() + 64, 48);
    bool plain = data[3] == 0x32 && (data[17] == 0x30 || data[17] == 0x23);
    if (!plain) aes128Ecb((const uint8_t *)CREALITY_AES_KEY, data, data, 48, true);
    String text;
    for (int i = 0; i < 48; i++) text += (char)(data[i] < 0x80 ? data[i] : '?');
    String length = text.substring(24, 28), color = text.substring(18, 24);
    if (!isDigits(length) || parseColor(color) < 0) return Filament();
    Filament f;
    f.format = "Creality";
    f.brand = "Creality";
    f.color = parseColor(color);
    f.diameter = 1.75f;
    int meters = length.toInt();
    f.weight = meters == 165 ? 500 : meters == 80 ? 250 : 1000;
    String code = text.substring(12, 17);
    if (isDigits(code)) f.presetId = code;
    for (auto &m : CREALITY_MATERIALS) {
        if (code != m.code) continue;
        f.material = m.material;
        f.modifiers = m.modifiers;
        f.nozzleMin = m.nmin;
        f.nozzleMax = m.nmax;
        f.bedTemp = m.bed;
    }
    return f;
}

// --- Snapmaker -------------------------------------------------------------

// The RSA signature over the data is not checked.
Filament decodeSnapmaker(const Tag &tag, const std::vector<NdefRecord> &) {
    if (!openedWith(tag, "Snapmaker")) return Filament();
    static const char *const TYPES[] = {nullptr, "PLA", "PETG", "ABS", "TPU", "PVA"};
    static const char *const SUBTYPES[] = {nullptr, "Basic", "Matte", "SnapSpeed", "Silk",
                                           "Support", "HF", "95A", "95A HF"};
    const uint8_t *d = tag.memory.data();
    uint16_t type = le16(d + 66), sub = le16(d + 68);
    if (type < 1 || type > 5) return Filament();
    Filament f;
    f.format = "Snapmaker";
    f.material = TYPES[type];
    if (sub >= 1 && sub <= 8) f.modifiers = SUBTYPES[sub];
    f.brand = ascii(d + 16, 16);
    if (!f.brand.length()) f.brand = "Snapmaker";
    f.color = rgb(d[80], d[81], d[82]);
    f.alpha = 0xFF - d[73];
    f.diameter = le16(d + 128) / 100.0f;
    if (f.diameter <= 0) f.diameter = 1.75f;
    f.weight = le16(d + 130);
    f.nozzleMax = le16(d + 148);
    f.nozzleMin = le16(d + 150);
    f.bedTemp = le16(d + 154);
    return f;
}

// --- Qidi ------------------------------------------------------------------

Filament decodeQidi(const Tag &tag, const std::vector<NdefRecord> &) {
    if (!openedWith(tag, DEFAULT_KEYSET_NAME)) return Filament();
    static const uint32_t COLORS[] = {
        0, 0xFAFAFA, 0x060606, 0xD9E3ED, 0x5CF30F, 0x63E492, 0x2850FF, 0xFE98FE, 0xDFD628,
        0x228332, 0x99DEFF, 0x1714B0, 0xCEC0FE, 0xCADE4B, 0x1353AB, 0x5EA9FD, 0xA878FF,
        0xFE717A, 0xFF362D, 0xE2DFCD, 0x898F9B, 0x6E3812, 0xCAC59F, 0xF28636, 0xB87F2B,
    };
    static const struct { uint8_t code; const char *material, *modifiers; } MATERIALS[] = {
        {0x01, "PLA", ""}, {0x02, "PLA", "Matte"}, {0x03, "PLA", "Metal"}, {0x04, "PLA", "Silk"},
        {0x05, "PLA-CF", ""}, {0x06, "PLA", "Wood"}, {0x07, "PLA", "Basic"},
        {0x08, "PLA", "Matte Basic"}, {0x0B, "ABS", ""}, {0x0C, "ABS-GF", ""},
        {0x0D, "ABS", "Metal"}, {0x0E, "ABS", "Odorless"}, {0x12, "ASA", ""}, {0x13, "ASA", "Aero"},
        {0x18, "PA", "Ultra"}, {0x19, "PA12-CF", ""}, {0x1A, "PA-CF", "Ultra CF25"},
        {0x1E, "PAHT-CF", ""}, {0x1F, "PAHT-GF", ""}, {0x20, "BVOH", "For PAHT"},
        {0x21, "BVOH", "For PET/PA"}, {0x22, "PC-ABS", "FR"}, {0x25, "PET-CF", ""},
        {0x26, "PET-GF", ""}, {0x27, "PETG", "Basic"}, {0x28, "PETG", "Tough"}, {0x29, "PETG", ""},
        {0x2C, "PPS-CF", ""}, {0x2D, "PETG", "Translucent"}, {0x2F, "PVA", ""},
        {0x31, "TPU", "Aero"}, {0x32, "TPU", ""},
    };
    const uint8_t *s = tag.memory.data() + 64;
    if (!s[0] || !s[1] || !s[2] || s[1] > 0x18) return Filament();
    for (int i = 3; i < 48; i++)
        if (s[i]) return Filament();
    for (auto &m : MATERIALS) {
        if (m.code != s[0]) continue;
        Filament f;
        f.format = "Qidi";
        f.material = m.material;
        f.modifiers = m.modifiers;
        f.brand = "QIDI";
        f.color = COLORS[s[1]];
        f.diameter = 1.75f;
        f.weight = 1000;
        return f;
    }
    return Filament();
}

// --- Anycubic --------------------------------------------------------------

Filament decodeAnycubic(const Tag &tag, const std::vector<NdefRecord> &) {
    const auto &m = tag.memory;
    if (tag.kind != TagKind::Type2 || m.size() < 0x80) return Filament();
    const uint8_t *d = m.data();
    if (d[0x10] != 0x7B || d[0x11] != 0x00 || d[0x12] != 0x65 || d[0x13] != 0x00) return Filament();
    Filament f;
    f.format = "Anycubic";
    String material = ascii(d + 0x3C, 16);
    material.replace("-", " ");
    normalizeMaterial(material, f.material, f.modifiers);
    if (!f.material.length()) f.material = "PLA";
    f.brand = ascii(d + 0x28, 16);
    if (!f.brand.length()) f.brand = "Anycubic";
    f.color = rgb(d[0x53], d[0x52], d[0x51]);
    f.alpha = d[0x50];
    f.nozzleMin = le16(d + 0x60);
    f.nozzleMax = le16(d + 0x62);
    f.bedTemp = le16(d + 0x76);
    f.diameter = le16(d + 0x78) / 100.0f;
    if (f.diameter <= 0) f.diameter = 1.75f;
    switch (le16(d + 0x7A)) {
    case 247: f.weight = 750; break;
    case 198: f.weight = 600; break;
    case 165: f.weight = 500; break;
    case 82: f.weight = 250; break;
    default: f.weight = 1000;
    }
    return f;
}

// --- Elegoo ----------------------------------------------------------------

Filament decodeElegoo(const Tag &tag, const std::vector<NdefRecord> &) {
    if (tag.kind != TagKind::Type2 || tag.memory.size() < 0x69) return Filament();
    const uint8_t *f = tag.memory.data() + 0x40;
    if (f[1] != 0xEE || f[2] != 0xEE || f[3] != 0xEE || f[4] != 0xEE) return Filament();
    static const char *const BASE[] = {"PLA", "PETG", "ABS", "TPU", "PA", "CPE", "PC", "PVA",
                                       "ASA", "BVOH", "EVA", "HIPS", "PP", "PPA", "PPS"};
    // material id, modifier id -> modifiers (fillers and PA sizes folded in below)
    static const struct { uint8_t base, mod; const char *mods; } MODS[] = {
        {0, 1, "+"}, {0, 2, "Pro"}, {0, 3, "Silk"}, {0, 4, "CF"}, {0, 5, "Carbon"}, {0, 6, "Matte"},
        {0, 7, "Fluo"}, {0, 8, "Wood"}, {0, 9, "Basic"}, {0, 10, "Rapid +"}, {0, 11, "Marble"},
        {0, 12, "Galaxy"}, {0, 13, "Red Copper"}, {0, 14, "Sparkle"},
        {1, 1, "CF"}, {1, 2, "GF"}, {1, 3, "Pro"}, {1, 4, "Translucent"}, {1, 5, "Rapid"},
        {2, 1, "GF"}, {3, 1, "95A"}, {3, 2, "Rapid 95A"},
        {4, 1, "CF"}, {4, 3, "HT CF"}, {4, 4, "6"}, {4, 5, "6 CF"}, {4, 6, "12"}, {4, 7, "12 CF"},
        {6, 1, "TG"}, {6, 2, "FR"}, {12, 1, "CF"}, {12, 2, "GF"}, {13, 1, "CF"}, {13, 2, "GF"},
        {14, 2, "CF"},
    };
    uint8_t base = f[0x0C], mod = f[0x0D];
    if (base >= sizeof(BASE) / sizeof(BASE[0])) return Filament();
    String mods;
    bool known = mod == 0;
    for (auto &m : MODS)
        if (m.base == base && m.mod == mod) {
            mods = m.mods;
            known = true;
        }
    if (!known) return Filament();
    Filament out;
    out.format = "Elegoo";
    out.material = BASE[base];
    // Fillers become part of the material, PA sizes too ("PA12-CF").
    String rest;
    int start = 0;
    while (start < (int)mods.length()) {
        int end = mods.indexOf(' ', start);
        if (end < 0) end = mods.length();
        String word = mods.substring(start, end);
        if (word == "CF" || word == "GF") out.material += "-" + word;
        else if ((word == "6" || word == "12") && out.material.startsWith("PA"))
            out.material = "PA" + word + out.material.substring(2);
        else addModifier(rest, word);
        start = end + 1;
    }
    out.modifiers = rest;
    out.brand = "Elegoo";
    out.color = rgb(f[0x10], f[0x11], f[0x12]);
    out.alpha = f[0x13];
    out.nozzleMin = be16(f + 0x14);
    out.nozzleMax = be16(f + 0x16);
    out.diameter = be16(f + 0x1C) / 100.0f;
    if (out.diameter <= 0) out.diameter = 1.75f;
    out.weight = be16(f + 0x1E);
    return out;
}

// --- Plain text ------------------------------------------------------------

bool materialChars(const String &text) {
    if (!text.length() || !isalpha((unsigned char)text[0])) return false;
    for (char c : text)
        if (!isalnum((unsigned char)c) && c != '+' && c != '-' && c != ' ') return false;
    return true;
}

// "PLA:FF0000", "PETG #00FF00", "PLA;FF0000;Brand", or a JSON object with
// material/type and color/color_hex keys.
Filament decodeText(const Tag &, const std::vector<NdefRecord> &records) {
    for (auto &record : records) {
        String mime = record.mime();
        if (!record.isText() && mime != "text/plain" && mime != "application/json") continue;
        String text = record.text();
        text.trim();
        Filament f;
        if (text.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, text)) continue;
            String protocol = doc["protocol"] | "";
            protocol.toLowerCase();
            if (protocol == "openspool") continue;
            String material = doc["material"] | (doc["type"] | "");
            int32_t color = parseColor(doc["color_hex"] | (doc["color"] | ""));
            if (!material.length() || color < 0) continue;
            f.format = "Text";
            normalizeMaterial(material, f.material, f.modifiers);
            f.color = color;
            f.brand = doc["brand"] | (doc["vendor"] | "");
            f.spoolId = jsonInt(doc["spool_id"]);
            return withDefaults(f);
        }
        for (int i = 2; i <= 16 && i < (int)text.length(); i++) {
            if (!strchr(":;,# ", text[i])) continue;
            String material = text.substring(0, i);
            material.trim();
            if (!materialChars(material)) continue;
            String rest = text.substring(i + 1);
            rest.trim();
            if (rest.startsWith("#")) rest = rest.substring(1);
            if (rest.length() < 6 || parseColor(rest.substring(0, 6)) < 0) continue;
            if (rest.length() > 6 && isalnum((unsigned char)rest[6])) continue;
            String brand = rest.substring(6);
            brand.trim();
            if (brand.length() && !strchr(":;,", brand[0])) continue;
            if (brand.length()) brand = brand.substring(1);
            brand.trim();
            f.format = "Text";
            normalizeMaterial(material, f.material, f.modifiers);
            f.color = parseColor(rest.substring(0, 6));
            f.brand = brand;
            return withDefaults(f);
        }
    }
    return Filament();
}

const Decoder DECODERS[] = {
    decodeOpenPrintTag, decodeOpenSpool, decodeSpoolEase, decodeBambu, decodeCreality,
    decodeSnapmaker, decodeAnycubic, decodeElegoo, decodeQidi, decodeText,
};

}  // namespace

int32_t parseColor(String text) {
    text.trim();
    if (text.startsWith("#")) text = text.substring(1);
    if (text.length() == 3) {
        String full;
        for (char c : text) {
            full += c;
            full += c;
        }
        text = full;
    }
    if (text.length() == 8) text = text.substring(0, 6);
    if (text.length() != 6) return -1;
    for (char c : text)
        if (!isxdigit((unsigned char)c)) return -1;
    return strtol(text.c_str(), nullptr, 16);
}

void normalizeMaterial(const String &input, String &material, String &modifiers) {
    static const char *const ALIASES[][2] = {
        {"PLA+", "PLA"}, {"PLA PLUS", "PLA"}, {"PLA-PLUS", "PLA"}, {"PLA-S", "PLA"},
        {"PETG+", "PETG"}, {"PET-G", "PETG"}, {"ABS+", "ABS"}, {"ABS-S", "ABS"},
        {"NYLON", "PA"}, {"PA-S", "PA"}, {"TPU-AMS", "TPU"}, {"TPU 95A", "TPU"},
        {"COPE", "CPE"}, {"PCTG+", "PCTG"},
    };
    auto alias = [&](const String &text) -> String {
        for (auto &a : ALIASES)
            if (text == a[0]) return a[1];
        return text;
    };
    String text = input;
    text.trim();
    text.toUpperCase();
    material = "";
    if (!text.length()) return;
    String aliased = alias(text);
    if (aliased != text) {
        material = aliased;
        return;
    }
    text.replace('_', ' ');
    int start = 0;
    bool first = true;
    while (start < (int)text.length()) {
        int end = text.indexOf(' ', start);
        if (end < 0) end = text.length();
        String word = text.substring(start, end);
        start = end + 1;
        if (!word.length()) continue;
        if (first) {
            material = word;
            first = false;
        } else if (word == "CF" || word == "GF") {
            material += "-" + word;
        } else {
            bool alpha = word.length() > 2;
            for (char c : word)
                if (!isalpha((unsigned char)c)) alpha = false;
            if (alpha) {
                String lower = word.substring(1);
                lower.toLowerCase();
                word = word.substring(0, 1) + lower;
            }
            addModifier(modifiers, word);
        }
    }
    if (material.endsWith("+")) {
        material.remove(material.length() - 1);
        addModifier(modifiers, "+");
    }
    material = alias(material);
}

Filament decodeTag(const Tag &tag) {
    std::vector<NdefRecord> records = tagRecords(tag);
    Filament reference = decodeSpoolman(tag, records);
    for (Decoder decoder : DECODERS) {
        Filament f = decoder(tag, records);
        if (!f.valid()) continue;
        // A Spoolman reference next to other data is merged into it.
        if (!f.spoolId) f.spoolId = reference.spoolId;
        if (!f.filamentId) f.filamentId = reference.filamentId;
        return f;
    }
    return reference;
}

void filamentToJson(const Filament &f, JsonObject out) {
    if (!f.valid()) return;
    out["format"] = f.format;
    out["title"] = f.title();
    if (f.material.length()) out["material"] = f.material;
    if (f.modifiers.length()) out["modifiers"] = f.modifiers;
    if (f.brand.length()) out["brand"] = f.brand;
    if (f.color >= 0) out["color"] = f.colorHex();
    if (f.alpha != 0xFF) out["alpha"] = f.alpha;
    if (f.diameter > 0) out["diameter"] = serialized(String(f.diameter, 2));
    if (f.weight) out["weight"] = f.weight;
    if (f.nozzleMin) out["nozzle_min"] = f.nozzleMin;
    if (f.nozzleMax) out["nozzle_max"] = f.nozzleMax;
    if (f.bedTemp) out["bed_temp"] = f.bedTemp;
    if (f.spoolId) out["spool_id"] = f.spoolId;
    if (f.filamentId) out["filament_id"] = f.filamentId;
}
