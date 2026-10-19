// zmod_esp32-nfc: an ESP32 + PN532 network NFC reader for Flashforge AD5X
// and Creator 5 printers.
//
// A scanned tag goes to the active printer (chosen on the web page):
//   zmod_nfc    the whole read goes to the zmod_nfc plugin, which decodes
//               it, looks it up in Spoolman and shows its slot prompt
//   zmod_basic  material and colour go to Zmod through Moonraker, plus
//               Moonraker's lane_data
//   stock       material and colour go to the stock firmware's local API
// For zmod_basic and stock the slot is picked on the web page, or set once
// as the automatic slot.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "app.h"
#include "decoders.h"

App app;

namespace {

constexpr uint32_t POLL_INTERVAL = 250;
constexpr uint32_t WIFI_WAIT = 20000;
// A scan nobody picked a slot for is dropped after this long.
constexpr uint32_t SLOT_WAIT = 10 * 60 * 1000;
// With the setup access point up, the home network is retried this often,
// only while nobody uses the access point (retries disturb it).
constexpr uint32_t WIFI_RETRY = 2 * 60 * 1000;
uint32_t lastWifiTry = 0;

void startAccessPoint() {
    uint64_t mac = ESP.getEfuseMac();
    char name[32];
    snprintf(name, sizeof(name), "zmod-nfc-setup-%02x%02x", (unsigned)(mac >> 32) & 0xFF,
             (unsigned)(mac >> 40) & 0xFF);
    app.apName = name;
    WiFi.mode(app.config.wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(name, "zmodnfc1");
    if (app.config.wifiSsid.length()) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        lastWifiTry = millis();
    }
    app.apMode = true;
    app.feedback.setIdleBlink(true);
    log_i("setup access point %s at %s", name, WiFi.softAPIP().toString().c_str());
}

void startWifi() {
    WiFi.persistent(false);
    WiFi.setHostname(app.config.hostname.c_str());
    if (!app.config.wifiSsid.length()) {
        startAccessPoint();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(app.config.wifiSsid.c_str(), app.config.wifiPass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_WAIT) {
        app.feedback.loop();
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(false);
        log_i("Wi-Fi %s, IP %s", app.config.wifiSsid.c_str(), WiFi.localIP().toString().c_str());
    } else {
        // Keep trying the network in the background, with the setup
        // access point open to fix the settings.
        startAccessPoint();
    }
}

}  // namespace

String App::readerName() const { return "zmod_esp32-nfc " + config.hostname; }

void App::record(const PrinterReply &reply) {
    last.ok = reply.ok;
    last.status = reply.message;
    if (reply.title.length()) last.printerTitle = reply.title;
    if (reply.spoolId) last.printerSpool = reply.spoolId;
    feedback.play(reply.ok ? Signal::Success : Signal::Failure);
}

void App::handleTag(const Tag &tag) {
    last = ScanRecord{};
    static uint32_t counter = 0;
    last.id = ++counter;
    last.at = millis();
    last.tag = tag;
    last.filament = decodeTag(tag);
    log_i("read %s -> %s", tag.describe().c_str(),
          last.filament.valid() ? last.filament.title().c_str() : "nothing decoded here");
    feedback.play(Signal::Read);

    if (tag.incomplete) {
        last.status = "The tag left the reader too soon; hold the spool still until it beeps";
        feedback.play(Signal::Failure);
        return;
    }
    const PrinterConfig *printer = config.activePrinter();
    if (!printer) {
        last.status = "No printer set up yet; add one on the Printers tab";
        return;
    }
    last.printer = config.active;
    if (printer->method == Method::ZmodNfc) {
        // The printer decodes the raw read itself; no need to know the format here.
        PrinterReply reply = sendScan(*printer, tag, readerName());
        last.waiting = reply.ok && reply.outcome == "prompt";
        record(reply);
        return;
    }
    if (!last.filament.hasFilamentData()) {
        last.status = last.filament.spoolId
                          ? "This tag only names Spoolman spool " + String(last.filament.spoolId) +
                                "; only the zmod_nfc method can look it up"
                          : "No filament data found on this tag";
        feedback.play(Signal::Failure);
        return;
    }
    if (config.autoSlot > 0) {
        record(assignSlot(*printer, tag, last.filament, config.autoSlot, readerName()));
        return;
    }
    last.waiting = true;
    last.ok = true;
    last.status = "Pick a slot on this page";
    feedback.play(Signal::Waiting);
}

PrinterReply App::assignLast(int index, int slot) {
    PrinterReply reply;
    if (!last.id) {
        reply.message = "Nothing scanned yet";
        return reply;
    }
    if (index < 0 || index >= (int)config.printers.size()) {
        reply.message = "No such printer";
        return reply;
    }
    const PrinterConfig &printer = config.printers[index];
    // A zmod_nfc printer that has not seen this tag yet gets it first.
    if (printer.method == Method::ZmodNfc && last.printer != index) {
        reply = sendScan(printer, last.tag, readerName());
        last.printer = index;
        if (!reply.ok) {
            record(reply);
            return reply;
        }
    }
    reply = assignSlot(printer, last.tag, last.filament, slot, readerName());
    last.printer = index;
    if (reply.ok) last.waiting = false;
    record(reply);
    return reply;
}

PrinterReply App::resendLast(int index) {
    PrinterReply reply;
    if (!last.id || index < 0 || index >= (int)config.printers.size()) {
        reply.message = "Nothing to send";
        return reply;
    }
    reply = sendScan(config.printers[index], last.tag, readerName());
    last.printer = index;
    last.waiting = reply.ok && reply.outcome == "prompt";
    record(reply);
    return reply;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    log_i("zmod_esp32-nfc %s", FIRMWARE_VERSION);
    loadConfig(app.config);
    app.feedback.begin(app.config.ledPin, app.config.ledActiveLow, app.config.buzzerPin);
    app.reader.begin(app.config.reader);
    startWifi();
    if (MDNS.begin(app.config.hostname.c_str())) MDNS.addService("http", "tcp", 80);
    webBegin();
}

void loop() {
    static uint32_t lastPoll = 0;
    webLoop();
    app.feedback.loop();
    if (app.apMode && app.config.wifiSsid.length()) {
        if (WiFi.status() == WL_CONNECTED) {
            // Back on the home network: close the setup access point.
            log_i("Wi-Fi %s, IP %s", app.config.wifiSsid.c_str(), WiFi.localIP().toString().c_str());
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false);
            app.apMode = false;
            app.feedback.setIdleBlink(false);
        } else if (millis() - lastWifiTry > WIFI_RETRY && WiFi.softAPgetStationNum() == 0) {
            lastWifiTry = millis();
            WiFi.begin(app.config.wifiSsid.c_str(), app.config.wifiPass.c_str());
        }
    }
    if (app.last.waiting && millis() - app.last.at > SLOT_WAIT) {
        app.last.waiting = false;
        app.last.status = "No slot was picked";
    }
    if (millis() - lastPoll < POLL_INTERVAL) return;
    lastPoll = millis();
    Tag tag;
    if (app.reader.poll(tag)) app.handleTag(tag);
}
