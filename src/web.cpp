// The device's web page and its JSON API.
#include <DNSServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "decoders.h"
#include "discovery.h"
#include "ota.h"
#include "version.h"
#include "voice.h"
#include "web_page.h"

namespace {

WebServer server(80);
DNSServer dns;
bool restartPending = false;
uint32_t restartAt = 0;

bool authorized() {
    // The page's own polling does not keep the reader from sleeping.
    if (server.uri() != "/api/status" && server.uri() != "/api/slots") app.usedAt = millis();
    if (!app.config.uiPassword.length()) return true;
    if (server.authenticate("admin", app.config.uiPassword.c_str())) return true;
    server.requestAuthentication(BASIC_AUTH, "LaneTag");
    return false;
}

void sendJson(int code, JsonDocument &doc) {
    String text;
    serializeJson(doc, text);
    server.send(code, "application/json", text);
}

void sendError(int code, const String &message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    sendJson(code, doc);
}

void sendReply(const PrinterReply &reply) {
    JsonDocument doc;
    doc["ok"] = reply.ok;
    doc["message"] = reply.message;
    sendJson(200, doc);
}

bool readBody(JsonDocument &doc) {
    if (deserializeJson(doc, server.arg("plain"))) {
        sendError(400, "the request is not JSON");
        return false;
    }
    return true;
}

int printerIndex(JsonDocument &doc) {
    int index = doc["printer"] | app.config.active;
    return index >= 0 && index < (int)app.config.printers.size() ? index : -1;
}

void restartSoon() {
    restartPending = true;
    restartAt = millis() + 1500;
}

// --- handlers --------------------------------------------------------------

void handleIndex() {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-cache");
    // In pieces: one write of the whole page (over 32 KB) never left the C3.
    constexpr size_t PIECE = 4096;
    size_t length = strlen_P(INDEX_HTML);
    server.setContentLength(length);
    server.send(200, "text/html", "");
    for (size_t sent = 0; sent < length; sent += PIECE)
        server.sendContent_P(INDEX_HTML + sent, std::min(PIECE, length - sent));
}

void handleStatus() {
    if (!authorized()) return;
    JsonDocument doc;
    doc["device"] = app.config.hostname;
    doc["version"] = FIRMWARE_VERSION;
    doc["board"] = FIRMWARE_BOARD;
    doc["ap"] = app.apMode;
    if (app.apMode) doc["ap_name"] = app.apName;
    if (app.wifiError.length()) doc["wifi_error"] = app.wifiError;
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ssid"] = app.config.wifiSsid;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["tx_power"] = (int)WiFi.getTxPower() / 4.0;
    JsonObject reader = doc["reader"].to<JsonObject>();
    reader["ok"] = app.reader.connected();
    reader["name"] = app.reader.describe();
    reader["error"] = app.reader.error();
    if (app.config.display.type != DisplayType::None) {
        JsonObject display = doc["display"].to<JsonObject>();
        display["ok"] = app.display.enabled();
        display["error"] = app.display.error();
    }
#ifdef HAS_VOICE
    doc["voice"] = true;
#endif
    doc["active"] = app.config.active;
    JsonArray printers = doc["printers"].to<JsonArray>();
    for (auto &p : app.config.printers) {
        JsonObject o = printers.add<JsonObject>();
        o["name"] = p.name;
        o["host"] = p.host;
        o["method"] = methodName(p.method);
        o["model"] = modelName(p.model);
        o["slot"] = p.slot;
        if (p.model == Model::Bambu || p.model == Model::Klipper || p.model == Model::Creality ||
            p.model == Model::Anycubic || p.model == Model::Qidi)
            o["unit"] = p.unit;
        if (p.model == Model::Klipper) o["slots_found"] = p.slotsFound;
    }
    const ScanRecord &last = app.last;
    if (last.id) {
        JsonObject scan = doc["scan"].to<JsonObject>();
        scan["id"] = last.id;
        scan["age"] = (millis() - last.at) / 1000;
        scan["uid"] = last.tag.uidHex();
        scan["kind"] = tagKindName(last.tag.kind);
        scan["describe"] = last.tag.describe();
        scan["printer"] = last.printer;
        scan["waiting"] = last.waiting;
        scan["ok"] = last.ok;
        scan["status"] = last.status;
        if (last.printerTitle.length()) scan["printer_title"] = last.printerTitle;
        if (last.printerSpool) scan["printer_spool"] = last.printerSpool;
        filamentToJson(last.filament, scan["filament"].to<JsonObject>());
    }
    sendJson(200, doc);
}

void handleGetConfig() {
    if (!authorized()) return;
    JsonDocument doc;
    configToJson(app.config, doc.to<JsonObject>(), false);
    sendJson(200, doc);
}

void handleSetConfig() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    bool restart = false;
    String error = configFromJson(app.config, doc.as<JsonObjectConst>(), restart);
    if (error.length()) {
        sendError(400, error);
        return;
    }
    // "Save and join": restart into the home network even when nothing changed.
    if (doc["join"] | false) restart = true;
    // New or changed Klipper printers: count their slots now, so the page
    // offers only those.
    if (!doc["printers"].isNull())
        for (auto &p : app.config.printers)
            if (p.model == Model::Klipper && !p.slotsFound) learnSlots(p, listSlots(p));
    saveConfig(app.config);
    applyWifiPower();          // takes effect at once
    JsonDocument reply;
    reply["ok"] = true;
    reply["restart"] = restart;
    sendJson(200, reply);
    if (restart) restartSoon();
}

void handleActive() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    int index = printerIndex(doc);
    if (index < 0) {
        sendError(400, "no such printer");
        return;
    }
    app.config.active = index;
    saveConfig(app.config);
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
}

void handleSlots() {
    if (!authorized()) return;
    int index = server.hasArg("printer") ? server.arg("printer").toInt() : app.config.active;
    if (index < 0 || index >= (int)app.config.printers.size()) {
        sendError(400, "no such printer");
        return;
    }
    PrinterConfig &printer = app.config.printers[index];
    SlotList list = listSlots(printer);
    if (learnSlots(printer, list)) saveConfig(app.config);
    JsonDocument doc;
    slotListToJson(list, doc.to<JsonObject>());
    sendJson(200, doc);
}

void handleAssign() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    int index = printerIndex(doc), slot = doc["slot"] | 0;
    if (index < 0 || slot < 1) {
        sendError(400, "printer and slot are needed");
        return;
    }
    sendReply(app.assignLast(index, slot));
}

// The active printer's fixed slot (0: ask), as on the Printers tab.
void handleFixedSlot() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    int index = printerIndex(doc), slot = doc["slot"] | -1;
    if (index < 0) {
        sendError(400, "no such printer");
        return;
    }
    const PrinterConfig &printer = app.config.printers[index];
    int first = printer.canAsk() ? 0 : 1;
    if (slot < first || slot > printer.slotCount()) {
        sendError(400, printer.slotCount() == 1 ? "this printer has one slot: 1"
                                                : "slot " + String(first) + " to " + String(printer.slotCount()) +
                                                      (first ? "" : " (0: ask)") + " is needed");
        return;
    }
    app.config.printers[index].slot = slot;
    saveConfig(app.config);
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
}

void handleResend() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    sendReply(app.resendLast(printerIndex(doc)));
}

void handleCancel() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    int index = printerIndex(doc);
    if (index >= 0) cancelPrompt(app.config.printers[index]);
    app.last.waiting = false;
    app.last.status = "Cancelled";
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
}

void handleTest() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    int index = printerIndex(doc);
    if (index < 0) {
        sendError(400, "no such printer; save it first");
        return;
    }
    PrinterConfig &printer = app.config.printers[index];
    SlotList list = listSlots(printer);
    if (learnSlots(printer, list)) saveConfig(app.config);
    PrinterReply reply = testReply(list);
    doc.clear();
    doc["ok"] = reply.ok;
    doc["message"] = reply.message;
    if (printer.model == Model::Klipper) doc["slots_found"] = printer.slotsFound;
    sendJson(200, doc);
}

void handleRescan() {
    if (!authorized()) return;
    app.reader.forget();
    app.quietUntil = millis();
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
}

void handleNetworks() {
    if (!authorized()) return;
    int count = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray list = doc["networks"].to<JsonArray>();
    for (int i = 0; i < count && i < 30; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;
        bool seen = false;
        for (JsonVariant v : list)
            if (ssid == v["ssid"].as<const char *>()) seen = true;
        if (seen) continue;
        JsonObject o = list.add<JsonObject>();
        o["ssid"] = ssid;
        o["rssi"] = WiFi.RSSI(i);
        o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    sendJson(200, doc);
}

void handleDiscover() {
    if (!authorized()) return;
    String error = discoveryStart();
    if (error.length()) {
        sendError(409, error);
        return;
    }
    JsonDocument doc;
    discoveryToJson(doc.to<JsonObject>());
    doc["ok"] = true;
    sendJson(200, doc);
}

void handleFound() {
    if (!authorized()) return;
    JsonDocument doc;
    discoveryToJson(doc.to<JsonObject>());
    doc["ok"] = true;
    sendJson(200, doc);
}

#ifdef HAS_VOICE
void handleVoice() {
    if (!authorized()) return;
    JsonDocument doc;
    doc["ok"] = true;
    doc["ready"] = voice::ready();
    if (voice::error().length()) doc["error"] = voice::error();
    doc["training"] = voice::training();
    doc["take"] = voice::trainingTake();
    doc["takes_needed"] = voice::TAKES;
    doc["takes_max"] = voice::MAX_TAKES;
    JsonArray commands = doc["commands"].to<JsonArray>();
    for (int c = 0; c < voice::COMMAND_COUNT; c++) {
        JsonObject o = commands.add<JsonObject>();
        o["name"] = voice::commandName(c);
        o["takes"] = voice::takes(c);
    }
    const voice::Result &last = voice::last();
    if (last.id) {
        JsonObject o = doc["last"].to<JsonObject>();
        o["id"] = last.id;
        o["heard"] = last.heard;
        o["action"] = last.action;
        o["distance"] = last.distance;
        o["second"] = last.second;
        o["frames"] = last.frames;
        o["peak"] = last.peak;
    }
    sendJson(200, doc);
}

// {"command": n} to train it, {"cancel": true}, or {"clear": n} (-1: all).
void handleVoicePost() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    if (doc["cancel"] | false) {
        voice::cancelTraining();
    } else if (!doc["clear"].isNull()) {
        voice::clear(doc["clear"] | -1);
    } else {
        int command = doc["command"] | -1;
        if (command < 0 || command >= voice::COMMAND_COUNT) {
            sendError(400, "no such command");
            return;
        }
        if (!voice::ready()) {
            sendError(400, voice::error().length() ? voice::error() : String("the microphone is not ready"));
            return;
        }
        voice::train(command, doc["more"] | false);
    }
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
}

// POST /api/voice/take?command=n&take=t with a WAV file (16 kHz mono
// 16-bit, as /api/voice/last.wav gives): a take made from it.
size_t uploadedSamples = 0;
String uploadError;

void handleVoiceTakeChunk() {
    HTTPUpload &up = server.upload();
    int16_t *buffer = voice::recordBuffer();
    size_t capacity = voice::recordCapacity() * 2;
    static size_t bytes = 0, skip = 0;
    if (up.status == UPLOAD_FILE_START) {
        bytes = 0;
        skip = 44;      // the WAV header
        uploadError = buffer ? "" : "not enough memory";
    } else if (up.status == UPLOAD_FILE_WRITE && buffer) {
        const uint8_t *data = up.buf;
        size_t len = up.currentSize;
        size_t drop = std::min(skip, len);
        skip -= drop;
        data += drop;
        len -= drop;
        len = std::min(len, capacity - bytes);
        memcpy((uint8_t *)buffer + bytes, data, len);
        bytes += len;
    } else if (up.status == UPLOAD_FILE_END) {
        uploadedSamples = bytes / 2;
    }
}

void handleVoiceTake() {
    if (!authorized()) return;
    if (uploadError.length()) {
        sendError(400, uploadError);
        return;
    }
    String error = voice::storeTake(server.arg("command").toInt(), server.arg("take").toInt(), uploadedSamples);
    if (error.length()) {
        sendError(400, error);
        return;
    }
    JsonDocument reply;
    reply["ok"] = true;
    reply["takes"] = voice::takes(server.arg("command").toInt());
    sendJson(200, reply);
}

void handleVoiceWav() {
    if (!authorized()) return;
    if (!voice::sampleCount()) {
        sendError(404, "nothing recorded yet");
        return;
    }
    uint8_t header[44];
    voice::wavHeader(header);
    server.setContentLength(voice::wavSize());
    server.send(200, "audio/wav", "");
    server.sendContent((const char *)header, sizeof(header));
    const char *data = (const char *)voice::samples();
    size_t bytes = voice::sampleCount() * 2;
    for (size_t sent = 0; sent < bytes; sent += 4096) server.sendContent(data + sent, std::min<size_t>(4096, bytes - sent));
}
#endif

void handleRestart() {
    if (!authorized()) return;
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
    restartSoon();
}

void handleOtaStart() {
    if (!authorized()) return;
    JsonDocument doc;
    if (!readBody(doc)) return;
    String error = otaStart(doc["url"] | "", doc["sha256"] | "");
    if (error.length()) {
        sendError(409, error);
        return;
    }
    JsonDocument reply;
    otaToJson(reply.to<JsonObject>());
    reply["ok"] = true;
    sendJson(200, reply);
}

void handleOtaState() {
    if (!authorized()) return;
    JsonDocument reply;
    otaToJson(reply.to<JsonObject>());
    reply["ok"] = true;
    sendJson(200, reply);
}

void handleUpdateDone() {
    if (!authorized()) return;
    if (otaRunning()) {
        sendError(409, "an update from GitHub is running");
        return;
    }
    bool ok = !Update.hasError();
    JsonDocument reply;
    reply["ok"] = ok;
    if (!ok) reply["error"] = Update.errorString();
    sendJson(ok ? 200 : 500, reply);
    if (ok) restartSoon();
}

void handleUpdateUpload() {
    if (app.config.uiPassword.length() && !server.authenticate("admin", app.config.uiPassword.c_str())) return;
    // Update serves one firmware at a time.
    if (otaRunning()) return;
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        log_i("firmware update: %s", upload.filename.c_str());
        Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        Update.end(true);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
    }
}

// Setup access point: every name resolves to the device, and unknown pages
// lead to the setup page, so phones open it as a captive portal.
void handleNotFound() {
    if (app.apMode) {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "not found");
}

}  // namespace

void webBegin() {
    server.on("/", HTTP_GET, handleIndex);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/config", HTTP_POST, handleSetConfig);
    server.on("/api/active", HTTP_POST, handleActive);
    server.on("/api/slots", HTTP_GET, handleSlots);
    server.on("/api/assign", HTTP_POST, handleAssign);
    server.on("/api/fixed_slot", HTTP_POST, handleFixedSlot);
    server.on("/api/resend", HTTP_POST, handleResend);
    server.on("/api/cancel", HTTP_POST, handleCancel);
    server.on("/api/test", HTTP_POST, handleTest);
    server.on("/api/rescan", HTTP_POST, handleRescan);
    server.on("/api/networks", HTTP_GET, handleNetworks);
    server.on("/api/discover", HTTP_POST, handleDiscover);
    server.on("/api/discover", HTTP_GET, handleFound);
    server.on("/api/restart", HTTP_POST, handleRestart);
#ifdef HAS_VOICE
    server.on("/api/voice", HTTP_GET, handleVoice);
    server.on("/api/voice", HTTP_POST, handleVoicePost);
    server.on("/api/voice/last.wav", HTTP_GET, handleVoiceWav);
    server.on("/api/voice/take", HTTP_POST, handleVoiceTake, handleVoiceTakeChunk);
#endif
    server.on("/api/ota", HTTP_POST, handleOtaStart);
    server.on("/api/ota", HTTP_GET, handleOtaState);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.onNotFound(handleNotFound);
    server.begin();
    if (app.apMode) dns.start(53, "*", WiFi.softAPIP());
}

void webLoop() {
    if (app.apMode) dns.processNextRequest();
    server.handleClient();
    if (restartPending && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
}
