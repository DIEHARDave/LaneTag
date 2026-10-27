#include "config.h"

#include <Preferences.h>
#include <nvs_flash.h>

#include "sleep.h"

namespace {

const char *const NVS_NAMESPACE = "lanetag";
// Where the settings lived before the rename to LaneTag; moved on first load.
const char *const OLD_NVS_NAMESPACE = "zmodnfc";
const char *const NVS_KEY = "config";

// Default pins per chip; all can be changed on the web page. The default
// button is the board's BOOT button.
#if defined(BOARD_C6_LCD147)
// Waveshare ESP32-C6-LCD-1.47: the screen takes 6, 7, 14, 15, 21 and 22 (and
// the SPI bus, so the reader is a PN532), the SD slot 4 and 5. The PN532
// goes on the header's TX/RX in HSU mode; the RGB LED is a WS2812 on 8.
constexpr int DEFAULT_SDA = 19, DEFAULT_SCL = 20, DEFAULT_RX = 17, DEFAULT_TX = 16;
constexpr int DEFAULT_SCK = -1, DEFAULT_MISO = -1, DEFAULT_MOSI = -1, DEFAULT_SS = -1, DEFAULT_RST = -1;
constexpr int DEFAULT_BUSY = -1;
constexpr int DEFAULT_LED = 8;
constexpr bool DEFAULT_LED_LOW = false;
constexpr int DEFAULT_BUTTON = 9;
const int SCREEN_PINS[] = {6, 7, 14, 15, 21, 22};
const char *const READER_PIN_KEYS[] = {"sda", "scl", "rx", "tx", "sck", "miso", "mosi", "ss", "rst", "busy"};

bool isScreenPin(int pin) {
    for (int p : SCREEN_PINS)
        if (p == pin) return true;
    return false;
}
#elif CONFIG_IDF_TARGET_ESP32S3
constexpr int DEFAULT_SDA = 8, DEFAULT_SCL = 9, DEFAULT_RX = 18, DEFAULT_TX = 17;
constexpr int DEFAULT_SCK = 12, DEFAULT_MISO = 13, DEFAULT_MOSI = 11, DEFAULT_SS = 10, DEFAULT_RST = 14;
constexpr int DEFAULT_BUSY = 15;
constexpr int DEFAULT_LED = -1;
constexpr bool DEFAULT_LED_LOW = false;
constexpr int DEFAULT_BUTTON = 0;
#elif CONFIG_IDF_TARGET_ESP32C3
// SuperMini: the blue LED is on GPIO 8 (lit when low), BOOT on GPIO 9.
constexpr int DEFAULT_SDA = 6, DEFAULT_SCL = 7, DEFAULT_RX = 20, DEFAULT_TX = 21;
constexpr int DEFAULT_SCK = 4, DEFAULT_MISO = 5, DEFAULT_MOSI = 6, DEFAULT_SS = 7, DEFAULT_RST = 3;
constexpr int DEFAULT_BUSY = 10;
constexpr int DEFAULT_LED = 8;
constexpr bool DEFAULT_LED_LOW = true;
constexpr int DEFAULT_BUTTON = 9;
#elif CONFIG_IDF_TARGET_ESP32C6
// GPIO 4, 5, 8, 9 and 15 are strapping pins and 12/13 are USB; BOOT is
// GPIO 9. The LED differs between boards, so none by default.
constexpr int DEFAULT_SDA = 6, DEFAULT_SCL = 7, DEFAULT_RX = 17, DEFAULT_TX = 16;
constexpr int DEFAULT_SCK = 21, DEFAULT_MISO = 20, DEFAULT_MOSI = 19, DEFAULT_SS = 18, DEFAULT_RST = 3;
constexpr int DEFAULT_BUSY = 2;
constexpr int DEFAULT_LED = -1;
constexpr bool DEFAULT_LED_LOW = false;
constexpr int DEFAULT_BUTTON = 9;
#elif defined(BOARD_CYD)
// ESP32-2432S028: a PN532 goes on CN1 (GPIO 22, 27, 3.3 V, GND). The screen
// and touch take most other pins; the SPI defaults are the SD card slot's.
// The RGB LED is lit when low: 4 red, 16 green, 17 blue (set below).
constexpr int DEFAULT_SDA = 27, DEFAULT_SCL = 22, DEFAULT_RX = 27, DEFAULT_TX = 22;
constexpr int DEFAULT_SCK = 18, DEFAULT_MISO = 19, DEFAULT_MOSI = 23, DEFAULT_SS = 5, DEFAULT_RST = -1;
constexpr int DEFAULT_BUSY = 35;
constexpr int DEFAULT_LED = 4;
constexpr bool DEFAULT_LED_LOW = true;
constexpr int DEFAULT_BUTTON = 0;
#else
constexpr int DEFAULT_SDA = 21, DEFAULT_SCL = 22, DEFAULT_RX = 16, DEFAULT_TX = 17;
constexpr int DEFAULT_SCK = 18, DEFAULT_MISO = 19, DEFAULT_MOSI = 23, DEFAULT_SS = 5, DEFAULT_RST = 4;
constexpr int DEFAULT_BUSY = 27;
constexpr int DEFAULT_LED = 2;
constexpr bool DEFAULT_LED_LOW = false;
constexpr int DEFAULT_BUTTON = 0;
#endif

int pin(JsonVariantConst value, int fallback) {
    if (value.isNull()) return fallback;
    int n = value.is<const char *>() ? atoi(value.as<const char *>()) : value.as<int>();
    return n >= -1 && n <= 48 ? n : fallback;
}

void setDefaults(Config &config) {
    config = Config();
    config.reader.sda = DEFAULT_SDA;
    config.reader.scl = DEFAULT_SCL;
    config.reader.rx = DEFAULT_RX;
    config.reader.tx = DEFAULT_TX;
    config.reader.sck = DEFAULT_SCK;
    config.reader.miso = DEFAULT_MISO;
    config.reader.mosi = DEFAULT_MOSI;
    config.reader.ss = DEFAULT_SS;
    config.reader.rst = DEFAULT_RST;
    config.reader.busy = DEFAULT_BUSY;
    config.led.pin = DEFAULT_LED;
    config.led.activeLow = DEFAULT_LED_LOW;
    config.buttonPin = DEFAULT_BUTTON;
    // An OLED most likely shares the reader's I2C pins.
    config.display.sda = DEFAULT_SDA;
    config.display.scl = DEFAULT_SCL;
#ifdef BOARD_C6_LCD147
    config.reader.bus = Pn532Bus::UART;
    config.display.type = DisplayType::C6Lcd147;
    config.led.type = LedType::Ws2812;
#endif
#ifdef BOARD_CYD
    config.reader.bus = Pn532Bus::UART;
    config.display.type = DisplayType::CydIli9341;
    config.led.type = LedType::Rgb;
    config.led.green = 16;
    config.led.blue = 17;
#endif
    // The last two bytes of the MAC address, as on the setup access point.
    uint64_t mac = ESP.getEfuseMac();
    char name[24];
    snprintf(name, sizeof(name), "lanetag-%02x%02x", (unsigned)(mac >> 32) & 0xFF, (unsigned)(mac >> 40) & 0xFF);
    config.hostname = name;
}

void secret(JsonObject out, const char *key, const String &value, bool secrets) {
    if (secrets)
        out[key] = value;
    else
        out[String(key) + "_set"] = value.length() > 0;
}

void readSecret(JsonObjectConst in, const char *key, String &value) {
    if (in[key].is<const char *>()) value = in[key].as<const char *>();
}

}  // namespace

const char *methodName(Method method) {
    switch (method) {
    case Method::ZmodNfc: return "zmod_nfc";
    case Method::ZmodBasic: return "zmod_basic";
    case Method::Snapmaker: return "snapmaker";
    case Method::Bambu: return "bambu";
    case Method::HappyHare: return "happy_hare";
    case Method::Afc: return "afc";
    case Method::LaneData: return "lane_data";
    case Method::CrealityCfs: return "creality_cfs";
    case Method::AnycubicAce: return "anycubic_ace";
    case Method::QidiBox: return "qidi_box";
    case Method::SnapmakerStock: return "snapmaker_stock";
    case Method::Spoolman: return "spoolman";
    default: return "stock";
    }
}

bool methodFits(Model model, Method method) {
    switch (model) {
    case Model::SnapmakerU1: return method == Method::Snapmaker || method == Method::SnapmakerStock;
    case Model::Bambu: return method == Method::Bambu;
    case Model::Klipper:
        return method == Method::HappyHare || method == Method::Afc || method == Method::LaneData;
    case Model::Adventurer5M: return method == Method::LaneData;
    case Model::Creality: return method == Method::CrealityCfs;
    case Model::Anycubic: return method == Method::AnycubicAce;
    case Model::Qidi: return method == Method::QidiBox;
    case Model::Spoolman: return method == Method::Spoolman;
    default: return method == Method::ZmodNfc || method == Method::ZmodBasic || method == Method::Stock;
    }
}

const char *modelName(Model model) {
    switch (model) {
    case Model::Creator5: return "creator5";
    case Model::SnapmakerU1: return "snapmaker_u1";
    case Model::Bambu: return "bambu";
    case Model::Klipper: return "klipper";
    case Model::Adventurer5M: return "ad5m";
    case Model::Creality: return "creality";
    case Model::Anycubic: return "anycubic";
    case Model::Qidi: return "qidi";
    case Model::Spoolman: return "spoolman";
    default: return "ad5x";
    }
}

uint16_t PrinterConfig::effectivePort() const {
    if (port) return port;
    switch (method) {
    case Method::ZmodNfc:
    case Method::ZmodBasic:
    case Method::Snapmaker:
    case Method::SnapmakerStock:
    case Method::HappyHare:
    case Method::Afc:
    case Method::LaneData:
    case Method::QidiBox: return MOONRAKER_PORT;
    case Method::Bambu: return BAMBU_PORT;
    case Method::CrealityCfs: return CREALITY_PORT;
    case Method::AnycubicAce: return ANYCUBIC_PORT;
    case Method::Spoolman: return SPOOLMAN_PORT;
    default: return FLASHFORGE_PORT;
    }
}

String PrinterConfig::baseUrl() const { return "http://" + host + ":" + String(effectivePort()); }

void configToJson(const Config &config, JsonObject out, bool secrets) {
    out["hostname"] = config.hostname;
    out["wifi_ssid"] = config.wifiSsid;
    secret(out, "wifi_pass", config.wifiPass, secrets);
    secret(out, "ui_password", config.uiPassword, secrets);
    out["wifi_power"] = config.wifiPower / 4.0;
    out["active"] = config.active;
    JsonObject reader = out["reader"].to<JsonObject>();
    reader["chip"] = config.reader.chip == ReaderChip::RC522    ? "rc522"
                     : config.reader.chip == ReaderChip::PN5180 ? "pn5180"
                                                                : "pn532";
    reader["bus"] = config.reader.bus == Pn532Bus::UART ? "uart" : "i2c";
    reader["sda"] = config.reader.sda;
    reader["scl"] = config.reader.scl;
    reader["rx"] = config.reader.rx;
    reader["tx"] = config.reader.tx;
    reader["sck"] = config.reader.sck;
    reader["miso"] = config.reader.miso;
    reader["mosi"] = config.reader.mosi;
    reader["ss"] = config.reader.ss;
    reader["rst"] = config.reader.rst;
    reader["busy"] = config.reader.busy;
    out["led_type"] = ledTypeName(config.led.type);
    out["led_pin"] = config.led.pin;
    out["led_green_pin"] = config.led.green;
    out["led_blue_pin"] = config.led.blue;
    out["led_active_low"] = config.led.activeLow;
    out["buzzer_pin"] = config.buzzerPin;
    out["button_pin"] = config.buttonPin;
    out["sleep_minutes"] = config.sleepMinutes;
    JsonObject display = out["display"].to<JsonObject>();
    display["type"] = displayTypeName(config.display.type);
    display["sda"] = config.display.sda;
    display["scl"] = config.display.scl;
    display["address"] = config.display.address;
    display["flip"] = config.display.flip;
    display["sleep"] = config.display.sleepMinutes;
    out["spoolman_report"] = config.spoolmanReport;
    JsonArray printers = out["printers"].to<JsonArray>();
    for (auto &p : config.printers) {
        JsonObject o = printers.add<JsonObject>();
        o["name"] = p.name;
        o["host"] = p.host;
        o["model"] = modelName(p.model);
        o["method"] = methodName(p.method);
        o["port"] = p.port;
        o["slot"] = p.slot;
        if (p.model == Model::Bambu || p.model == Model::Klipper || p.model == Model::Creality ||
            p.model == Model::Anycubic || p.model == Model::Qidi)
            o["unit"] = p.unit;
        if (p.model == Model::Klipper) o["slots_found"] = p.slotsFound;
        if (p.method == Method::Afc) o["next_lane"] = p.nextLane;
        if (p.model == Model::Spoolman) {
            o["create"] = p.spoolmanCreate;
            o["location"] = p.location;
        }
        o["serial"] = p.serial;
        secret(o, "check_code", p.checkCode, secrets);
        secret(o, "api_key", p.apiKey, secrets);
    }
}

String configFromJson(Config &config, JsonObjectConst in, bool &restart) {
    Config next = config;
    if (in["hostname"].is<const char *>()) {
        String name = in["hostname"].as<const char *>();
        name.trim();
        for (char c : name)
            if (!isalnum((unsigned char)c) && c != '-') return "the device name may only have letters, digits and '-'";
        if (!name.length() || name.length() > 31) return "the device name must be 1 to 31 characters";
        next.hostname = name;
    }
    if (in["wifi_ssid"].is<const char *>()) next.wifiSsid = in["wifi_ssid"].as<const char *>();
    readSecret(in, "wifi_pass", next.wifiPass);
    readSecret(in, "ui_password", next.uiPassword);
    if (!in["wifi_power"].isNull()) {
        int quarter = (int)(in["wifi_power"].as<float>() * 4 + 0.5f);
        if (quarter != 0 && (quarter < 8 || quarter > 78)) return "Wi-Fi power must be 2 to 19.5 dBm";
        next.wifiPower = quarter;
    }
    JsonObjectConst reader = in["reader"];
    if (!reader.isNull()) {
        String chip = reader["chip"] | "pn532";
        next.reader.chip = chip == "rc522"    ? ReaderChip::RC522
                           : chip == "pn5180" ? ReaderChip::PN5180
                                              : ReaderChip::PN532;
        String bus = reader["bus"] | "i2c";
        next.reader.bus = bus == "uart" ? Pn532Bus::UART : Pn532Bus::I2C;
        next.reader.sda = pin(reader["sda"], next.reader.sda);
        next.reader.scl = pin(reader["scl"], next.reader.scl);
        next.reader.rx = pin(reader["rx"], next.reader.rx);
        next.reader.tx = pin(reader["tx"], next.reader.tx);
        next.reader.sck = pin(reader["sck"], next.reader.sck);
        next.reader.miso = pin(reader["miso"], next.reader.miso);
        next.reader.mosi = pin(reader["mosi"], next.reader.mosi);
        next.reader.ss = pin(reader["ss"], next.reader.ss);
        next.reader.rst = pin(reader["rst"], next.reader.rst);
        next.reader.busy = pin(reader["busy"], next.reader.busy);
        bool spi = next.reader.chip != ReaderChip::PN532;
        if (spi && (next.reader.sck < 0 || next.reader.miso < 0 || next.reader.mosi < 0 || next.reader.ss < 0))
            return next.reader.chip == ReaderChip::RC522 ? "the RC522 needs its SCK, MISO, MOSI and SDA (SS) pins"
                                                         : "the PN5180 needs its SCK, MISO, MOSI and NSS pins";
        if (next.reader.chip == ReaderChip::PN5180 && next.reader.busy < 0) return "the PN5180 needs its BUSY pin";
#ifdef BOARD_C6_LCD147
        if (spi) return "the screen has this board's SPI bus; use a PN532 (HSU or I2C)";
        for (int p : {next.reader.sda, next.reader.scl, next.reader.rx, next.reader.tx})
            if (isScreenPin(p)) return "GPIO " + String(p) + " is the screen's on this board";
#endif
    }
    if (in["led_type"].is<const char *>()) {
        String type = in["led_type"].as<const char *>();
        next.led.type = type == "rgb" ? LedType::Rgb : type == "ws2812" ? LedType::Ws2812 : LedType::Single;
    } else if (!in["led_pin"].isNull()) {
        // Saved before there were LED types: the one LED pin.
        next.led.type = LedType::Single;
    }
    next.led.pin = pin(in["led_pin"], next.led.pin);
    next.led.green = pin(in["led_green_pin"], next.led.green);
    next.led.blue = pin(in["led_blue_pin"], next.led.blue);
    if (!in["led_active_low"].isNull()) next.led.activeLow = in["led_active_low"].as<bool>();
    next.buzzerPin = pin(in["buzzer_pin"], next.buzzerPin);
    next.buttonPin = pin(in["button_pin"], next.buttonPin);
    next.sleepMinutes = constrain(in["sleep_minutes"] | next.sleepMinutes, 0, 1440);
    if (next.sleepMinutes && !canWakeFrom(next.buttonPin))
        return next.buttonPin < 0 ? String("sleep needs the slot button to wake the board")
                                  : "the slot button on GPIO " + String(next.buttonPin) +
                                        " cannot wake this board from sleep; use " + wakePins();
    JsonObjectConst display = in["display"];
    if (!display.isNull()) {
        String type = display["type"] | "none";
        next.display.type = type == "ssd1306"      ? DisplayType::SSD1306
                            : type == "sh1106"     ? DisplayType::SH1106
                            : type == "cyd"        ? DisplayType::CydIli9341
                            : type == "cyd-st7789" ? DisplayType::CydSt7789
                            : type == "c6-lcd147"  ? DisplayType::C6Lcd147
                                                   : DisplayType::None;
#ifndef HAS_TFT
        if (isTft(next.display.type)) return "this firmware has no touch screen support (use the esp32dev build)";
#elif defined(BOARD_C6_LCD147)
        if (isTft(next.display.type) && next.display.type != DisplayType::C6Lcd147)
            return "this firmware drives only the board's own 1.47\" screen";
#else
        if (next.display.type == DisplayType::C6Lcd147)
            return "the 1.47\" screen needs the esp32-c6-lcd build";
#endif
        next.display.sda = pin(display["sda"], next.display.sda);
        next.display.scl = pin(display["scl"], next.display.scl);
        int address = display["address"] | (int)next.display.address;
        if (address < 0x08 || address > 0x77) return "the display address must be 0x08 to 0x77 (usually 0x3C)";
        next.display.address = address;
        if (!display["flip"].isNull()) next.display.flip = display["flip"].as<bool>();
        next.display.sleepMinutes = constrain(display["sleep"] | next.display.sleepMinutes, 0, 1440);
        if (next.display.type != DisplayType::None && !isTft(next.display.type) &&
            (next.display.sda < 0 || next.display.scl < 0))
            return "the display needs its SDA and SCL pins";
    }
    if (in["spoolman_report"].is<const char *>()) {
        next.spoolmanReport = in["spoolman_report"].as<const char *>();
        next.spoolmanReport.trim();
        if (next.spoolmanReport.indexOf(' ') >= 0) return "the Spoolman address has a space in it";
    }
    JsonArrayConst printers = in["printers"];
    if (!printers.isNull()) {
        if (printers.size() > MAX_PRINTERS) return "at most " + String(MAX_PRINTERS) + " printers";
        std::vector<PrinterConfig> list;
        for (size_t i = 0; i < printers.size(); i++) {
            JsonObjectConst o = printers[i];
            // Secrets the page did not send stay as they were for the
            // printer at the same position (the page sends "keep" as index).
            int keep = o["keep"] | -1;
            PrinterConfig p = keep >= 0 && keep < (int)config.printers.size() ? config.printers[keep]
                                                                              : PrinterConfig();
            p.name = o["name"] | "";
            p.host = o["host"] | "";
            p.name.trim();
            p.host.trim();
            String model = o["model"] | "ad5x";
            String method = o["method"] | "zmod_nfc";
            p.model = model == "creator5"       ? Model::Creator5
                      : model == "snapmaker_u1" ? Model::SnapmakerU1
                      : model == "bambu"        ? Model::Bambu
                      : model == "klipper"      ? Model::Klipper
                      : model == "ad5m"         ? Model::Adventurer5M
                      : model == "creality"     ? Model::Creality
                      : model == "anycubic"     ? Model::Anycubic
                      : model == "qidi"         ? Model::Qidi
                      : model == "spoolman"     ? Model::Spoolman
                                                : Model::AD5X;
            p.method = method == "stock"        ? Method::Stock
                       : method == "zmod_basic" ? Method::ZmodBasic
                       : method == "snapmaker"  ? Method::Snapmaker
                       : method == "bambu"      ? Method::Bambu
                       : method == "happy_hare" ? Method::HappyHare
                       : method == "afc"        ? Method::Afc
                       : method == "lane_data"  ? Method::LaneData
                       : method == "creality_cfs" ? Method::CrealityCfs
                       : method == "anycubic_ace" ? Method::AnycubicAce
                       : method == "qidi_box"   ? Method::QidiBox
                       : method == "snapmaker_stock" ? Method::SnapmakerStock
                       : method == "spoolman"   ? Method::Spoolman
                                                : Method::ZmodNfc;
            // The U1, Bambu and Klipper printers take only their own methods.
            if (!methodFits(p.model, p.method)) {
                switch (p.model) {
                case Model::SnapmakerU1: p.method = Method::Snapmaker; break;
                case Model::Bambu: p.method = Method::Bambu; break;
                case Model::Creality: p.method = Method::CrealityCfs; break;
                case Model::Anycubic: p.method = Method::AnycubicAce; break;
                case Model::Qidi: p.method = Method::QidiBox; break;
                case Model::Spoolman: p.method = Method::Spoolman; break;
                case Model::Klipper:
                case Model::Adventurer5M: p.method = Method::LaneData; break;
                default: p.method = Method::ZmodNfc; break;
                }
            }
            p.port = constrain(o["port"] | 0, 0, 65535);
            bool external = p.model == Model::Bambu || p.model == Model::Creality;
            p.unit = constrain(o["unit"] | p.unit, external ? EXTERNAL_SPOOL : 1, 4);
            if (p.model == Model::Adventurer5M || p.model == Model::Spoolman) p.unit = 1;
            if (!o["create"].isNull()) p.spoolmanCreate = o["create"].as<bool>();
            if (!o["next_lane"].isNull()) p.nextLane = o["next_lane"].as<bool>();
            if (o["location"].is<const char *>()) {
                p.location = o["location"].as<const char *>();
                p.location.trim();
            }
            p.slotsFound = constrain(o["slots_found"] | p.slotsFound, 0, SLOT_COUNT);
            // Another printer, method or group of four: count its slots again.
            if (keep >= 0 && keep < (int)config.printers.size()) {
                const PrinterConfig &old = config.printers[keep];
                if (old.host != p.host || old.port != p.port || old.method != p.method || old.unit != p.unit)
                    p.slotsFound = 0;
            }
            // Settings saved before the fixed slot moved into each printer
            // had one auto_slot for all printers.
            p.slot = constrain(o["slot"] | (in["auto_slot"] | 0), p.canAsk() ? 0 : 1, p.slotCount());
            p.serial = o["serial"] | "";
            p.serial.trim();
            readSecret(o, "check_code", p.checkCode);
            readSecret(o, "api_key", p.apiKey);
            if (!p.host.length()) return "printer " + String(i + 1) + " has no IP address";
            if (!p.name.length()) p.name = p.host;
            if (p.method == Method::Stock && (!p.serial.length() || !p.checkCode.length()))
                return p.name + ": stock firmware needs the serial number and check code";
            if (p.method == Method::Bambu && (!p.serial.length() || !p.checkCode.length()))
                return p.name + ": a Bambu printer needs its serial number and access code";
            list.push_back(p);
        }
        next.printers = list;
    }
    if (!in["active"].isNull()) next.active = in["active"].as<int>();
    if (next.active >= (int)next.printers.size()) next.active = 0;

    restart = next.wifiSsid != config.wifiSsid || next.wifiPass != config.wifiPass ||
              next.hostname != config.hostname || next.reader.chip != config.reader.chip ||
              next.reader.bus != config.reader.bus ||
              next.reader.sda != config.reader.sda || next.reader.scl != config.reader.scl ||
              next.reader.rx != config.reader.rx || next.reader.tx != config.reader.tx ||
              next.reader.sck != config.reader.sck || next.reader.miso != config.reader.miso ||
              next.reader.mosi != config.reader.mosi || next.reader.ss != config.reader.ss ||
              next.reader.rst != config.reader.rst || next.reader.busy != config.reader.busy ||
              next.led.type != config.led.type || next.led.pin != config.led.pin ||
              next.led.green != config.led.green || next.led.blue != config.led.blue ||
              next.led.activeLow != config.led.activeLow || next.buzzerPin != config.buzzerPin ||
              next.buttonPin != config.buttonPin || next.display.type != config.display.type ||
              next.display.sda != config.display.sda || next.display.scl != config.display.scl ||
              next.display.address != config.display.address || next.display.flip != config.display.flip ||
              next.display.sleepMinutes != config.display.sleepMinutes;
    config = next;
    return "";
}

String slotName(const PrinterConfig &printer, int slot) {
    if (slot <= 0) return "Ask";
    // As the printers name them: toolheads T0-T3, IFS slots 1-4.
    switch (printer.model) {
    case Model::Creator5: return "T" + String(slot - 1);
    case Model::SnapmakerU1: return "Head " + String(slot);
    case Model::Bambu:
        return printer.unit == EXTERNAL_SPOOL ? String("Ext") : String((char)('A' + printer.unit - 1)) + String(slot);
    case Model::Adventurer5M: return "T0";
    case Model::Spoolman: return "Spoolman";
    case Model::Anycubic:
        return printer.unit <= 1 ? "Slot " + String(slot) : "ACE " + String(printer.unit) + " slot " + String(slot);
    case Model::Qidi:
        return printer.unit <= 1 ? "Slot " + String(slot) : "Box " + String(printer.unit) + " slot " + String(slot);
    case Model::Creality:
        // As Creality names them: CFS 1 slots 1A-1D.
        return printer.unit == EXTERNAL_SPOOL ? String("Ext") : String(printer.unit) + (char)('A' + slot - 1);
    case Model::Klipper: {
        int index = printer.slotIndex(slot);
        if (printer.method == Method::HappyHare) return "Gate " + String(index);
        if (printer.method == Method::Afc) return "lane" + String(index + 1);
        return "T" + String(index);
    }
    default: return "Slot " + String(slot);
    }
}

String slotShortName(const PrinterConfig &printer, int slot) {
    if (slot <= 0) return "Ask";
    switch (printer.model) {
    case Model::Creator5:
    case Model::Bambu:
    case Model::Creality:
    case Model::Adventurer5M: return slotName(printer, slot);
    case Model::Spoolman: return "SM";
    case Model::Anycubic:
    case Model::Qidi: return printer.unit <= 1 ? String(slot) : String(printer.unit) + "-" + String(slot);
    case Model::Klipper: {
        int index = printer.slotIndex(slot);
        if (printer.method == Method::HappyHare) return String(index);
        if (printer.method == Method::Afc) return String(index + 1);
        return "T" + String(index);
    }
    default: return String(slot);
    }
}

bool takesSpoolOnlyTags(Method method) {
    return method == Method::Snapmaker || method == Method::HappyHare || method == Method::Afc ||
           method == Method::LaneData || method == Method::Spoolman;
}

void loadConfig(Config &config) {
    setDefaults(config);
    String text;
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        text = prefs.getString(NVS_KEY, "");
        prefs.end();
    }
    if (!text.length()) {
        // Settings saved by zmod_esp32-nfc firmware: copy them over once.
        if (!prefs.begin(OLD_NVS_NAMESPACE, true)) return;
        text = prefs.getString(NVS_KEY, "");
        prefs.end();
        if (!text.length()) return;
        bool moved = prefs.begin(NVS_NAMESPACE, false) && prefs.putString(NVS_KEY, text);
        prefs.end();
        if (moved && prefs.begin(OLD_NVS_NAMESPACE, false)) {
            prefs.clear();
            prefs.end();
        }
        log_w("settings %s from the old \"%s\" storage", moved ? "moved" : "read", OLD_NVS_NAMESPACE);
    }
    JsonDocument doc;
    if (deserializeJson(doc, text)) {
        log_w("stored settings are not valid JSON; using defaults");
        return;
    }
#ifdef BOARD_C6_LCD147
    // Saved by the plain ESP32-C6 firmware, whose reader pins are this
    // board's screen pins (I2C there crashes the I2C driver): back to the
    // defaults for those.
    for (const char *key : READER_PIN_KEYS)
        if (isScreenPin(doc["reader"][key] | -1)) doc["reader"].remove(key);
#endif
    bool restart;
    String error = configFromJson(config, doc.as<JsonObjectConst>(), restart);
    if (error.length()) log_w("stored settings: %s", error.c_str());
}

void saveConfig(const Config &config) {
    JsonDocument doc;
    configToJson(config, doc.to<JsonObject>(), true);
    String text;
    serializeJson(doc, text);
    for (int attempt = 0; attempt < 2; attempt++) {
        Preferences prefs;
        bool saved = prefs.begin(NVS_NAMESPACE, false) && prefs.putString(NVS_KEY, text);
        prefs.end();
        if (saved) return;
        if (attempt) break;
        // NVS full or unreadable, as left by a board's previous firmware:
        // start it afresh (its other contents are only Wi-Fi driver caches).
        log_e("settings storage full or unusable; erasing it");
        nvs_flash_erase();
        nvs_flash_init();
    }
    log_e("settings not saved (%u bytes)", text.length());
}
