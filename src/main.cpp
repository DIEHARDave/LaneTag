// LaneTag: an ESP32 + PN532, RC522 or PN5180 network NFC/RFID filament tag
// reader for 3D printers (formerly zmod_esp32-nfc).
//
// A scanned tag goes to the active printer (chosen on the web page):
//   zmod_nfc    the whole read goes to the zmod_nfc plugin, which decodes
//               it, looks it up in Spoolman and shows its slot prompt
//   zmod_basic  material and color go to Zmod through Moonraker, plus
//               Moonraker's lane_data
//   stock       material and color go to the stock firmware's local API
//   snapmaker   the Snapmaker U1 (Extended firmware) gets the read as its
//               head's own RFID read; SpoolLink matches it in Spoolman
//   snapmaker_stock
//               a stock U1 gets the head's type, vendor and color
//               (SET_PRINT_FILAMENT_CONFIG through Moonraker)
//   bambu       a Bambu Lab printer in LAN mode gets the AMS tray's material,
//               color and preset over its MQTT broker
//   anycubic_ace
//               an Anycubic Kobra in LAN mode gets the ACE slot's material and
//               color over its MQTT broker
//   qidi_box    a Qidi Plus4 / Q2 / Max4 gets the Box slot's filament, color
//               and vendor ids (save_variables) through Moonraker
//   creality_cfs
//               a Creality K1 / K2 / Hi gets the CFS slot's material and color
//               over Creality Print's WebSocket
//   happy_hare, afc, lane_data
//               Klipper printers through Moonraker: Happy Hare's gate map,
//               AFC's lanes, or plain lane_data (Spoolman through each)
// For zmod_basic and stock the slot is picked on the web page, or set once
// as the automatic slot; the U1 always has a fixed head. The board's button
// steps that slot (see buttonLoop).
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "app.h"
#include "decoders.h"

App app;

namespace {

constexpr uint32_t POLL_INTERVAL = 250;
// Some routers turn away the first joins after a restart (AUTH_EXPIRE,
// handshake timeouts) and only let one in after 10-20 s.
constexpr uint32_t WIFI_WAIT = 30000;
// A scan nobody picked a slot for is dropped after this long.
constexpr uint32_t SLOT_WAIT = 10 * 60 * 1000;
// With the setup access point up, the home network is retried this often.
// A retry briefly disturbs the access point, so it waits longer while
// someone is connected to it, but never stops (a phone left on the setup
// page would otherwise keep the reader off the network).
constexpr uint32_t WIFI_RETRY = 30 * 1000;
constexpr uint32_t WIFI_RETRY_IN_USE = 2 * 60 * 1000;
// The home network gone this long while running: open the setup access
// point too (auto-reconnect alone can stay stuck when the router keeps
// turning the reader away).
constexpr uint32_t WIFI_LOST = 60 * 1000;
uint32_t lastWifiTry = 0;
uint32_t wifiLostAt = 0;

// The button steps the active printer's slot: ask, 1 ... its slot count, ask.
// A first press only shows the slot on the LED (one blink per slot number,
// one long blink for ask); each press within STEP_WINDOW of the last moves
// to the next slot. The choice is saved once the button has rested.
constexpr uint32_t DEBOUNCE = 30;
constexpr uint32_t STEP_WINDOW = 5000;

void buttonPressed(bool step) {
    if (app.config.active < 0 || app.config.active >= (int)app.config.printers.size()) {
        app.feedback.play(Signal::Failure);
        return;
    }
    PrinterConfig &printer = app.config.printers[app.config.active];
    if (step) {
        printer.slot = (printer.slot + 1) % (printer.slotCount() + 1);
        if (!printer.slot && !printer.canAsk()) printer.slot = 1;
        log_i("button: %s slot %d", printer.name.c_str(), printer.slot);
    }
    app.feedback.count(printer.slot);
    app.display.showSlot();
}

void buttonLoop() {
    static bool down = false, unsaved = false;
    static uint32_t changedAt = 0, lastPress = 0;
    if (app.config.buttonPin < 0) return;
    uint32_t now = millis();
    bool pressed = digitalRead(app.config.buttonPin) == LOW;
    if (pressed != down && now - changedAt > DEBOUNCE) {
        down = pressed;
        changedAt = now;
        if (pressed) {
            bool step = lastPress && now - lastPress < STEP_WINDOW;
            buttonPressed(step);
            unsaved |= step;
            lastPress = now;
        }
    }
    if (unsaved && now - lastPress > STEP_WINDOW) {
        saveConfig(app.config);
        unsaved = false;
    }
}

void setWifiPower() { applyWifiPower(); }

String wifiFailure() {
    switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL: return "network not found (the ESP32 only sees 2.4 GHz networks)";
    case WL_CONNECT_FAILED: return "connection refused (wrong password?)";
    default: return "no answer from the network (wrong password, or too far away?)";
    }
}

void startAccessPoint() {
    uint64_t mac = ESP.getEfuseMac();
    char name[32];
    snprintf(name, sizeof(name), "lanetag-setup-%02x%02x", (unsigned)(mac >> 32) & 0xFF,
             (unsigned)(mac >> 40) & 0xFF);
    app.apName = name;
    WiFi.mode(app.config.wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
    setWifiPower();
    WiFi.softAP(name, "lanetag1");
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
    setWifiPower();
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
        app.wifiError = wifiFailure();
        log_w("Wi-Fi %s: %s", app.config.wifiSsid.c_str(), app.wifiError.c_str());
        startAccessPoint();
    }
}

}  // namespace

// Quarter dBm: WIFI_POWER_15dBm.
#ifndef C3_DEFAULT_WIFI_POWER
#define C3_DEFAULT_WIFI_POWER 60
#endif

// Call after every WiFi.mode() change, which resets the transmit power.
void applyWifiPower() {
    if (app.config.wifiPower) {
        WiFi.setTxPower((wifi_power_t)app.config.wifiPower);
        return;
    }
#if CONFIG_IDF_TARGET_ESP32C3
    // C3 SuperMini boards have a poor antenna match, and the power that
    // works differs between boards: 8.5 dBm (the usual advice) lost 80% of
    // packets on one, which worked without loss from 11 to 19.5 dBm. Other
    // values: Settings > Wi-Fi transmit power.
    WiFi.setTxPower((wifi_power_t)C3_DEFAULT_WIFI_POWER);
#endif
}

String App::readerName() const { return "LaneTag " + config.hostname; }

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
    // Sending can take seconds (zmod_nfc waits for Spoolman); show the
    // filament meanwhile.
    auto sending = [&] {
        last.ok = true;
        last.status = "Sending to " + printer->name + "...";
        display.refresh();
    };
    if (printer->method == Method::ZmodNfc) {
        sending();
        // The printer decodes the raw read itself; no need to know the format
        // here. A reader fixed to a slot skips the printer's prompt.
        PrinterReply reply = sendScan(*printer, tag, readerName(), printer->slot);
        last.waiting = reply.ok && reply.outcome == "prompt";
        record(reply);
        return;
    }
    // The U1's SpoolLink and the Klipper methods can look up a tag that only
    // names a Spoolman spool.
    if (!last.filament.hasFilamentData() && !takesSpoolOnlyTags(printer->method)) {
        last.status = last.filament.spoolId
                          ? "This tag only names Spoolman spool " + String(last.filament.spoolId) +
                                "; only the zmod_nfc method can look it up"
                          : "No filament data found on this tag";
        feedback.play(Signal::Failure);
        return;
    }
    if (printer->slot > 0) {
        sending();
        record(assignSlot(*printer, tag, last.filament, printer->slot, readerName()));
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
    reply = sendScan(config.printers[index], last.tag, readerName(), config.printers[index].slot);
    last.printer = index;
    last.waiting = reply.ok && reply.outcome == "prompt";
    record(reply);
    return reply;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    // 1 power-on, 3 software restart, 4 crash, 5-7 watchdog, 9 brownout.
    log_i("LaneTag %s, reset reason %d", FIRMWARE_VERSION, (int)esp_reset_reason());
    loadConfig(app.config);
    app.feedback.begin(app.config.ledPin, app.config.ledActiveLow, app.config.buzzerPin);
    if (app.config.buttonPin >= 0) pinMode(app.config.buttonPin, INPUT_PULLUP);
    app.reader.begin(app.config.reader);
    app.display.begin(app.config.display, app.config.reader);
    startWifi();
    // The clock, for printers whose LAN handshake is signed over the time
    // (Anycubic); SNTP keeps trying in the background.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    if (MDNS.begin(app.config.hostname.c_str())) MDNS.addService("http", "tcp", 80);
    webBegin();
}

void loop() {
    static uint32_t lastPoll = 0;
    webLoop();
    app.feedback.loop();
    app.display.loop();
    buttonLoop();
    if (app.apMode && app.config.wifiSsid.length()) {
        if (WiFi.status() == WL_CONNECTED) {
            // Back on the home network: close the setup access point.
            log_i("Wi-Fi %s, IP %s", app.config.wifiSsid.c_str(), WiFi.localIP().toString().c_str());
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            setWifiPower();
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false);
            app.apMode = false;
            app.wifiError = "";
            app.feedback.setIdleBlink(false);
        } else if (millis() - lastWifiTry > (WiFi.softAPgetStationNum() ? WIFI_RETRY_IN_USE : WIFI_RETRY)) {
            lastWifiTry = millis();
            WiFi.begin(app.config.wifiSsid.c_str(), app.config.wifiPass.c_str());
        }
    } else if (!app.apMode && app.config.wifiSsid.length()) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiLostAt = 0;
        } else if (!wifiLostAt) {
            wifiLostAt = millis() | 1;
        } else if (millis() - wifiLostAt > WIFI_LOST) {
            wifiLostAt = 0;
            app.wifiError = wifiFailure();
            log_w("Wi-Fi %s lost: %s", app.config.wifiSsid.c_str(), app.wifiError.c_str());
            startAccessPoint();
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
