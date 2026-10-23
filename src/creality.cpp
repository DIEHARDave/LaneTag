// Creality K1 / K2 / Hi printers on stock firmware.
//
// Creality Print's device manager talks to these printers over a WebSocket
// on port 9999: JSON requests {"method": "get" | "set", "params": {...}},
// answered by objects keyed like the request. "boxsInfo" lists the CFS units
// and their slots, "reqMaterials" the printer's material library, and
// "modifyMaterial" writes one slot, as Creality Print does when a slot is
// edited (protocol as documented by heydemoura/creality-orca-integration and
// used by 3dg1luk43/ha_creality_ws).
//
// Status messages can be large (the material library, a full status frame),
// so they are parsed straight from the socket through ArduinoJson filters,
// as the Bambu reports are.
#include "creality.h"

#include <WiFi.h>
#include <mbedtls/base64.h>
#include <vector>

namespace {

constexpr uint32_t TIMEOUT = 8000;
// The external spool holder's box.
constexpr int EXTERNAL_BOX_TYPE = 1;
// materials[].state: 2 = a Creality RFID spool, which the CFS reads itself.
constexpr int SLOT_RFID = 2;

String upper(String text) {
    text.trim();
    text.toUpperCase();
    return text;
}

// --- WebSocket ---------------------------------------------------------------------

class WebSocket;

// One message's payload, across continuation frames, for ArduinoJson.
class MessageStream : public Stream {
public:
    MessageStream(WebSocket &ws, size_t length, bool fin) : ws_(ws), left_(length), fin_(fin) {}
    int available() override { return left_ ? 1 : 0; }
    int read() override;
    int peek() override { return -1; }
    size_t write(uint8_t) override { return 0; }
    bool finish();
    // The start of the message, to recognize bare text such as "heart_beat".
    String prefix;

private:
    WebSocket &ws_;
    size_t left_;
    bool fin_;
    bool failed_ = false;
};

class WebSocket {
public:
    String error;

    bool open(const String &host, uint16_t port) {
        if (WiFi.status() != WL_CONNECTED) return fail("Wi-Fi is not connected");
        if (!tcp_.connect(host.c_str(), port, 3000))
            return fail("no connection to " + host + ":" + String(port) + " (is it a Creality K1/K2/Hi?)");
        uint8_t key[16];
        for (auto &b : key) b = esp_random();
        unsigned char key64[32];
        size_t len = 0;
        mbedtls_base64_encode(key64, sizeof(key64), &len, key, sizeof(key));
        String request = "GET / HTTP/1.1\r\nHost: " + host + ":" + String(port) +
                         "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                         String((const char *)key64).substring(0, len) + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        tcp_.print(request);
        uint32_t deadline = millis() + TIMEOUT;
        String status, line;
        uint8_t b;
        // The response headers, up to the empty line.
        while (readByte(b, deadline)) {
            if (b == '\r') continue;
            if (b != '\n') {
                if (line.length() < 200) line += (char)b;
                continue;
            }
            if (!status.length()) status = line;
            else if (!line.length()) {
                if (status.indexOf(" 101") < 0) return fail("the printer refused the WebSocket (" + status + ")");
                return true;
            }
            line = "";
        }
        return fail("no WebSocket handshake from the printer");
    }

    bool sendText(const String &text) {
        std::vector<uint8_t> frame{0x81};   // FIN, text
        size_t len = text.length();
        if (len < 126) {
            frame.push_back(0x80 | len);
        } else {
            frame.push_back(0x80 | 126);
            frame.push_back(len >> 8);
            frame.push_back(len & 0xFF);
        }
        uint8_t mask[4];
        for (auto &b : mask) b = esp_random();
        frame.insert(frame.end(), mask, mask + 4);
        for (size_t i = 0; i < len; i++) frame.push_back(text[i] ^ mask[i % 4]);
        if (tcp_.write(frame.data(), frame.size()) != frame.size()) return fail("lost the printer's connection");
        return true;
    }

    bool send(const JsonDocument &doc) {
        String text;
        serializeJson(doc, text);
        return sendText(text);
    }

    // The next JSON message through filter. Heart beats are answered and
    // skipped, as is anything that is not JSON. False on timeout or a lost
    // connection.
    bool receive(JsonDocument &doc, const JsonDocument &filter, uint32_t deadline) {
        uint8_t opcode;
        bool fin;
        size_t length;
        while (readFrame(opcode, fin, length, deadline)) {
            if (opcode == 0x1 || opcode == 0x2) {
                MessageStream message(*this, length, fin);
                DeserializationError err = deserializeJson(doc, message, DeserializationOption::Filter(filter));
                if (!message.finish()) return false;
                if (message.prefix.startsWith("heart_beat") || (!err && doc["ModeCode"] == "heart_beat")) {
                    sendText("ok");
                    continue;
                }
                if (!err && doc.size()) return true;
            } else if (!skip(length)) {
                return false;
            }
        }
        return false;
    }

    void close() {
        if (tcp_.connected()) {
            uint8_t frame[6] = {0x88, 0x80, 0, 0, 0, 0};    // close, masked, empty
            tcp_.write(frame, sizeof(frame));
        }
        tcp_.stop();
    }

    ~WebSocket() { close(); }

    // The next frame of a fragmented message (control frames in between are
    // handled here).
    bool nextFragment(size_t &length, bool &fin) {
        uint8_t opcode;
        while (readFrame(opcode, fin, length, millis() + TIMEOUT)) {
            if (opcode == 0x0) return true;
            if (!skip(length)) return false;
        }
        return false;
    }

    bool readByte(uint8_t &b, uint32_t deadline) {
        while (!tcp_.available()) {
            if (!tcp_.connected() || (int32_t)(millis() - deadline) >= 0) return false;
            delay(2);
        }
        b = tcp_.read();
        return true;
    }

private:
    WiFiClient tcp_;

    bool fail(const String &message) {
        error = message;
        return false;
    }

    bool skip(size_t length) {
        uint32_t deadline = millis() + TIMEOUT;
        uint8_t b;
        while (length--)
            if (!readByte(b, deadline)) return false;
        return true;
    }

    // A frame header; pings are answered and closes end the connection, so
    // only data frames come back.
    bool readFrame(uint8_t &opcode, bool &fin, size_t &length, uint32_t deadline) {
        while (true) {
            uint8_t h[2];
            if (!readByte(h[0], deadline) || !readByte(h[1], millis() + TIMEOUT)) return false;
            fin = h[0] & 0x80;
            opcode = h[0] & 0x0F;
            uint64_t len = h[1] & 0x7F;
            int extra = len == 126 ? 2 : len == 127 ? 8 : 0;
            if (extra) {
                len = 0;
                for (int i = 0; i < extra; i++) {
                    uint8_t b;
                    if (!readByte(b, millis() + TIMEOUT)) return false;
                    len = (len << 8) | b;
                }
            }
            uint8_t mask[4] = {};
            if (h[1] & 0x80)    // servers do not mask; skip it if one does
                for (auto &m : mask)
                    if (!readByte(m, millis() + TIMEOUT)) return false;
            length = (size_t)len;
            if (opcode == 0x8) return fail("the printer closed the connection");
            if (opcode == 0x9) {    // ping: answer with the same payload
                std::vector<uint8_t> payload(length);
                for (auto &b : payload)
                    if (!readByte(b, millis() + TIMEOUT)) return false;
                std::vector<uint8_t> pong{0x8A, (uint8_t)(0x80 | (length & 0x7F)), 0, 0, 0, 0};
                pong.insert(pong.end(), payload.begin(), payload.end());
                tcp_.write(pong.data(), pong.size());
                continue;
            }
            return true;
        }
    }
};

int MessageStream::read() {
    if (failed_) return -1;
    while (!left_) {
        if (fin_ || !ws_.nextFragment(left_, fin_)) {
            if (!fin_) failed_ = true;
            return -1;
        }
    }
    uint8_t b;
    if (!ws_.readByte(b, millis() + TIMEOUT)) {
        failed_ = true;
        return -1;
    }
    left_--;
    if (prefix.length() < 16) prefix += (char)b;
    return b;
}

// Skips what the parser left of the message. False when the connection
// broke.
bool MessageStream::finish() {
    while (!failed_ && read() >= 0) {
    }
    return !failed_;
}

// --- Printer state -------------------------------------------------------------

void statusFilter(JsonDocument &filter) {
    filter["ModeCode"] = true;
    filter["deviceState"] = true;
    filter["feedState"] = true;
    filter["model"] = true;
    filter["modifyMaterial"] = true;
    JsonObject box = filter["boxsInfo"]["materialBoxs"][0].to<JsonObject>();
    box["id"] = true;
    box["type"] = true;
    box["state"] = true;
    JsonObject slot = box["materials"][0].to<JsonObject>();
    for (const char *key : {"id", "type", "name", "vendor", "color", "state", "rfid", "minTemp", "maxTemp"})
        slot[key] = true;
}

void libraryFilter(JsonDocument &filter) {
    JsonObject base = filter["retMaterials"][0]["base"].to<JsonObject>();
    for (const char *key : {"id", "brand", "name", "meterialType", "minTemp", "maxTemp"}) base[key] = true;
    filter["retMaterials"][0]["kvParam"]["pressure_advance"] = true;
}

struct CrealityState {
    JsonDocument boxes;         // boxsInfo.materialBoxs
    JsonDocument library;       // retMaterials
    int deviceState = -1;       // 0: idle
    int feedState = -1;         // 0, 3, 14: no filament moving
    String model;
};

// Asks for the CFS state (and the material library) and collects the
// answers, which may come in separate messages among the status pushes.
bool readState(WebSocket &ws, CrealityState &state, bool library, String &error) {
    JsonDocument request;
    request["method"] = "get";
    request["params"]["boxsInfo"] = 1;
    if (library) request["params"]["reqMaterials"] = 1;
    if (!ws.send(request)) {
        error = ws.error;
        return false;
    }
    JsonDocument filter, msg;
    statusFilter(filter);
    if (library) libraryFilter(filter);
    bool haveBoxes = false, haveLibrary = !library;
    uint32_t deadline = millis() + TIMEOUT, settle = 0;
    while (ws.receive(msg, filter, deadline)) {
        if (msg["deviceState"].is<int>()) state.deviceState = msg["deviceState"];
        if (msg["feedState"].is<int>()) state.feedState = msg["feedState"];
        if (msg["model"].is<const char *>()) state.model = msg["model"].as<String>();
        if (msg["boxsInfo"]["materialBoxs"].is<JsonArrayConst>()) {
            state.boxes.set(msg["boxsInfo"]["materialBoxs"]);
            haveBoxes = true;
        }
        if (msg["retMaterials"].is<JsonArrayConst>()) {
            state.library.set(msg["retMaterials"]);
            haveLibrary = true;
        }
        if (haveBoxes && haveLibrary) {
            // The idle state comes with the printer's first status push; give
            // it a moment if it has not shown up yet.
            if (state.deviceState >= 0) return true;
            if (!settle) settle = millis() + 1500;
            deadline = settle;
        }
    }
    if (haveBoxes && haveLibrary) return true;
    error = ws.error.length() ? ws.error : String("the printer did not send its CFS state (is a CFS connected?)");
    return false;
}

JsonObjectConst findBox(const CrealityState &state, const PrinterConfig &printer) {
    for (JsonObjectConst box : state.boxes.as<JsonArrayConst>()) {
        bool external = (box["type"] | -1) == EXTERNAL_BOX_TYPE;
        if (printer.unit == EXTERNAL_SPOOL ? external : !external && (box["id"] | -1) == printer.unit) return box;
    }
    return JsonObjectConst();
}

JsonObjectConst findSlot(JsonObjectConst box, int slot) {
    for (JsonObjectConst m : box["materials"].as<JsonArrayConst>())
        if ((m["id"] | -1) == slot - 1) return m;
    return JsonObjectConst();
}

// "#0RRGGBB" (the printer's form) or "#RRGGBB" to "RRGGBB".
String slotColor(String color) {
    color.replace("#", "");
    if (color.length() == 7) color = color.substring(1);
    color.toUpperCase();
    return color.length() == 6 ? color : String();
}

SlotInfo slotInfo(const PrinterConfig &printer, int slot, JsonObjectConst m) {
    SlotInfo info;
    info.slot = slot;
    info.label = slotName(printer, slot);
    String name = m["name"] | "", type = m["type"] | "";
    info.material = name.length() ? name : type;
    info.color = info.material.length() ? slotColor(m["color"] | "") : String();
    info.loaded = (m["state"] | 0) > 0;
    return info;
}

String noBox(const PrinterConfig &printer) {
    return printer.unit == EXTERNAL_SPOOL ? String("the printer reports no external spool holder")
                                          : "the printer reports no CFS " + String(printer.unit);
}

// --- Material library -------------------------------------------------------------

bool hasWord(const String &haystack, const String &words) {
    String h = upper(haystack);
    int start = 0;
    String w = upper(words);
    while (start < (int)w.length()) {
        int end = w.indexOf(' ', start);
        if (end < 0) end = w.length();
        String word = w.substring(start, end);
        if (word.length() > 1 && h.indexOf(word) >= 0) return true;
        start = end + 1;
    }
    return false;
}

// The library entry for a tag: its own material id (Creality tags), else the
// same brand and material (one naming the tag's modifiers first), else a
// Generic one of the material, else any of the material.
JsonObjectConst libraryEntry(const CrealityState &state, const Filament &f) {
    JsonArrayConst library = state.library.as<JsonArrayConst>();
    if (f.presetId.length())
        for (JsonObjectConst e : library)
            if (f.presetId == (e["base"]["id"] | "")) return e;
    String material = upper(f.material.length() ? f.material : String("PLA"));
    JsonObjectConst sameBrand, generic, any;
    for (JsonObjectConst e : library) {
        if (upper(e["base"]["meterialType"] | "") != material) continue;
        String brand = e["base"]["brand"] | "", name = e["base"]["name"] | "";
        if (f.brand.length() && brand.equalsIgnoreCase(f.brand)) {
            if (f.modifiers.length() && hasWord(name, f.modifiers)) return e;
            if (sameBrand.isNull()) sameBrand = e;
        }
        if (generic.isNull() && brand.equalsIgnoreCase("Generic")) generic = e;
        if (any.isNull()) any = e;
    }
    return !sameBrand.isNull() ? sameBrand : !generic.isNull() ? generic : any;
}

}  // namespace

SlotList crealitySlots(const PrinterConfig &printer) {
    SlotList list;
    WebSocket ws;
    if (!ws.open(printer.host, printer.effectivePort())) {
        list.error = ws.error;
        return list;
    }
    CrealityState state;
    if (!readState(ws, state, false, list.error)) return list;
    JsonObjectConst box = findBox(state, printer);
    if (box.isNull()) {
        list.error = noBox(printer);
        return list;
    }
    for (int slot = 1; slot <= printer.slotCount(); slot++) {
        JsonObjectConst m = findSlot(box, slot);
        if (m.isNull()) break;
        list.slots.push_back(slotInfo(printer, slot, m));
    }
    if (list.slots.empty()) {
        list.error = noBox(printer);
        return list;
    }
    list.ok = true;
    return list;
}

PrinterReply crealityAssign(const PrinterConfig &printer, const Filament &f, int slot) {
    PrinterReply reply;
    if (slot < 1 || slot > printer.slotCount()) {
        reply.message = "no such slot";
        return reply;
    }
    if (!f.hasFilamentData()) {
        reply.message = f.spoolId ? "The tag only names Spoolman spool " + String(f.spoolId) +
                                        "; Creality's stock firmware cannot look it up"
                                  : "No filament data on this tag";
        return reply;
    }
    WebSocket ws;
    if (!ws.open(printer.host, printer.effectivePort())) {
        reply.message = ws.error;
        return reply;
    }
    CrealityState state;
    if (!readState(ws, state, true, reply.message)) return reply;
    // As Creality Print: no edits while printing or while filament moves.
    if (state.deviceState > 0) {
        reply.message = "The printer is busy (printing or moving); slots are not changed now";
        return reply;
    }
    if (state.feedState > 0 && state.feedState != 3 && state.feedState != 14) {
        reply.message = "The CFS is feeding filament; try again when it has finished";
        return reply;
    }
    JsonObjectConst box = findBox(state, printer);
    JsonObjectConst current = box.isNull() ? JsonObjectConst() : findSlot(box, slot);
    if (current.isNull()) {
        reply.message = noBox(printer);
        return reply;
    }
    String label = slotName(printer, slot);
    if ((current["state"] | 0) == SLOT_RFID) {
        reply.message = label + " holds a Creality RFID spool, which the CFS reads itself";
        return reply;
    }

    JsonObjectConst entry = libraryEntry(state, f);
    String material = upper(f.material.length() ? f.material : String("PLA"));
    String brand = entry["base"]["brand"] | "";
    // The library's name when the entry is the tag's own filament, else the
    // tag's.
    bool own = !entry.isNull() && (!f.brand.length() || brand.equalsIgnoreCase(f.brand));
    String name = own ? String(entry["base"]["name"] | "") : f.title();
    String hex = f.color >= 0 ? f.colorHex() : String("FFFFFF");
    JsonDocument cmd;
    cmd["method"] = "set";
    JsonObject m = cmd["params"]["modifyMaterial"].to<JsonObject>();
    m["boxId"] = box["id"] | 0;
    m["id"] = slot - 1;
    if (!entry.isNull()) m["rfid"] = entry["base"]["id"] | "";
    m["vendor"] = f.brand.length() ? f.brand : brand.length() ? brand : String("Generic");
    m["type"] = material;
    m["name"] = name.length() ? name : material;
    // "#0rrggbb", lower case, as Creality Print writes it.
    String color = "#0" + hex;
    color.toLowerCase();
    m["color"] = color;
    m["minTemp"] = f.nozzleMin ? f.nozzleMin : (entry["base"]["minTemp"] | 190);
    m["maxTemp"] = f.nozzleMax ? f.nozzleMax : (entry["base"]["maxTemp"] | 230);
    String pressure = entry["kvParam"]["pressure_advance"] | "";
    if (pressure.length()) m["pressure"] = pressure.toFloat();
    if (!ws.send(cmd)) {
        reply.message = ws.error;
        return reply;
    }

    // The printer answers {"modifyMaterial": code} and pushes a new
    // boxsInfo; the slot is checked in a fresh read either way.
    CrealityState after;
    if (!readState(ws, after, false, reply.message)) return reply;
    ws.close();
    box = findBox(after, printer);
    current = box.isNull() ? JsonObjectConst() : findSlot(box, slot);
    if (current.isNull()) {
        reply.message = noBox(printer);
        return reply;
    }
    SlotInfo now = slotInfo(printer, slot, current);
    if (upper(current["type"] | "") != material || now.color != hex) {
        reply.message = label + " is still " + (now.material.length() ? now.material + " #" + now.color : "empty") +
                        "; the printer did not take the change";
        return reply;
    }
    reply.ok = true;
    reply.title = now.material;
    reply.message = label + " is now " + now.material + " #" + now.color;
    if (!entry.isNull()) reply.message += " (" + String(entry["base"]["name"] | "") + " profile)";
    return reply;
}
