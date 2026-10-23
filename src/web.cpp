// The device's web page and its JSON API.
#include <DNSServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "decoders.h"
#include "web_page.h"

namespace {

WebServer server(80);
DNSServer dns;
bool restartPending = false;
uint32_t restartAt = 0;

bool authorized() {
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

void handleRestart() {
    if (!authorized()) return;
    JsonDocument reply;
    reply["ok"] = true;
    sendJson(200, reply);
    restartSoon();
}

void handleUpdateDone() {
    if (!authorized()) return;
    bool ok = !Update.hasError();
    JsonDocument reply;
    reply["ok"] = ok;
    if (!ok) reply["error"] = Update.errorString();
    sendJson(ok ? 200 : 500, reply);
    if (ok) restartSoon();
}

void handleUpdateUpload() {
    if (app.config.uiPassword.length() && !server.authenticate("admin", app.config.uiPassword.c_str())) return;
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
    server.on("/api/restart", HTTP_POST, handleRestart);
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
