#include "config.h"

#include <Preferences.h>

namespace {

const char *const NVS_NAMESPACE = "zmodnfc";
const char *const NVS_KEY = "config";

// Default pins per chip; all can be changed on the web page.
#if CONFIG_IDF_TARGET_ESP32S3
constexpr int DEFAULT_SDA = 8, DEFAULT_SCL = 9, DEFAULT_RX = 18, DEFAULT_TX = 17;
constexpr int DEFAULT_LED = -1;
constexpr bool DEFAULT_LED_LOW = false;
#elif CONFIG_IDF_TARGET_ESP32C3
// SuperMini: the blue LED is on GPIO 8 (lit when low).
constexpr int DEFAULT_SDA = 6, DEFAULT_SCL = 7, DEFAULT_RX = 20, DEFAULT_TX = 21;
constexpr int DEFAULT_LED = 8;
constexpr bool DEFAULT_LED_LOW = true;
#else
constexpr int DEFAULT_SDA = 21, DEFAULT_SCL = 22, DEFAULT_RX = 16, DEFAULT_TX = 17;
constexpr int DEFAULT_LED = 2;
constexpr bool DEFAULT_LED_LOW = false;
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
    config.ledPin = DEFAULT_LED;
    config.ledActiveLow = DEFAULT_LED_LOW;
    // The last two bytes of the MAC address, as on the setup access point.
    uint64_t mac = ESP.getEfuseMac();
    char name[24];
    snprintf(name, sizeof(name), "zmod-nfc-%02x%02x", (unsigned)(mac >> 32) & 0xFF, (unsigned)(mac >> 40) & 0xFF);
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
    default: return "stock";
    }
}

const char *modelName(Model model) { return model == Model::Creator5 ? "creator5" : "ad5x"; }

uint16_t PrinterConfig::effectivePort() const {
    if (port) return port;
    switch (method) {
    case Method::ZmodNfc:
    case Method::ZmodBasic: return MOONRAKER_PORT;
    default: return FLASHFORGE_PORT;
    }
}

String PrinterConfig::baseUrl() const { return "http://" + host + ":" + String(effectivePort()); }

void configToJson(const Config &config, JsonObject out, bool secrets) {
    out["hostname"] = config.hostname;
    out["wifi_ssid"] = config.wifiSsid;
    secret(out, "wifi_pass", config.wifiPass, secrets);
    secret(out, "ui_password", config.uiPassword, secrets);
    out["active"] = config.active;
    out["auto_slot"] = config.autoSlot;
    JsonObject reader = out["reader"].to<JsonObject>();
    reader["bus"] = config.reader.bus == Pn532Bus::UART ? "uart" : "i2c";
    reader["sda"] = config.reader.sda;
    reader["scl"] = config.reader.scl;
    reader["rx"] = config.reader.rx;
    reader["tx"] = config.reader.tx;
    out["led_pin"] = config.ledPin;
    out["led_active_low"] = config.ledActiveLow;
    out["buzzer_pin"] = config.buzzerPin;
    JsonArray printers = out["printers"].to<JsonArray>();
    for (auto &p : config.printers) {
        JsonObject o = printers.add<JsonObject>();
        o["name"] = p.name;
        o["host"] = p.host;
        o["model"] = modelName(p.model);
        o["method"] = methodName(p.method);
        o["port"] = p.port;
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
    if (!in["auto_slot"].isNull()) next.autoSlot = constrain(in["auto_slot"].as<int>(), 0, 16);
    JsonObjectConst reader = in["reader"];
    if (!reader.isNull()) {
        String bus = reader["bus"] | "i2c";
        next.reader.bus = bus == "uart" ? Pn532Bus::UART : Pn532Bus::I2C;
        next.reader.sda = pin(reader["sda"], next.reader.sda);
        next.reader.scl = pin(reader["scl"], next.reader.scl);
        next.reader.rx = pin(reader["rx"], next.reader.rx);
        next.reader.tx = pin(reader["tx"], next.reader.tx);
    }
    next.ledPin = pin(in["led_pin"], next.ledPin);
    if (!in["led_active_low"].isNull()) next.ledActiveLow = in["led_active_low"].as<bool>();
    next.buzzerPin = pin(in["buzzer_pin"], next.buzzerPin);
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
            p.model = model == "creator5" ? Model::Creator5 : Model::AD5X;
            p.method = method == "stock" ? Method::Stock : method == "zmod_basic" ? Method::ZmodBasic : Method::ZmodNfc;
            p.port = constrain(o["port"] | 0, 0, 65535);
            p.serial = o["serial"] | "";
            p.serial.trim();
            readSecret(o, "check_code", p.checkCode);
            readSecret(o, "api_key", p.apiKey);
            if (!p.host.length()) return "printer " + String(i + 1) + " has no IP address";
            if (!p.name.length()) p.name = p.host;
            if (p.method == Method::Stock && (!p.serial.length() || !p.checkCode.length()))
                return p.name + ": stock firmware needs the serial number and check code";
            list.push_back(p);
        }
        next.printers = list;
    }
    if (!in["active"].isNull()) next.active = in["active"].as<int>();
    if (next.active >= (int)next.printers.size()) next.active = 0;

    restart = next.wifiSsid != config.wifiSsid || next.wifiPass != config.wifiPass ||
              next.hostname != config.hostname || next.reader.bus != config.reader.bus ||
              next.reader.sda != config.reader.sda || next.reader.scl != config.reader.scl ||
              next.reader.rx != config.reader.rx || next.reader.tx != config.reader.tx ||
              next.ledPin != config.ledPin || next.buzzerPin != config.buzzerPin;
    config = next;
    return "";
}

void loadConfig(Config &config) {
    setDefaults(config);
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;
    String text = prefs.getString(NVS_KEY, "");
    prefs.end();
    if (!text.length()) return;
    JsonDocument doc;
    if (deserializeJson(doc, text)) {
        log_w("stored settings are not valid JSON; using defaults");
        return;
    }
    bool restart;
    String error = configFromJson(config, doc.as<JsonObjectConst>(), restart);
    if (error.length()) log_w("stored settings: %s", error.c_str());
}

void saveConfig(const Config &config) {
    JsonDocument doc;
    configToJson(config, doc.to<JsonObject>(), true);
    String text;
    serializeJson(doc, text);
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY, text);
    prefs.end();
}
