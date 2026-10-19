// Settings, kept as JSON in NVS and edited on the web page.
#pragma once

#include <ArduinoJson.h>
#include <vector>

#include "reader.h"

// How a scan reaches a printer.
enum class Method : uint8_t {
    ZmodNfc = 0,    // Zmod + zmod_nfc plugin: the raw tag read goes to the plugin's
                    // listener, which decodes it, looks it up in Spoolman and shows
                    // its slot prompt, like a USB reader on the printer
    ZmodBasic = 1,  // Zmod without the plugin: colour and material through
                    // Moonraker (CHANGE_ZCOLOR) and Moonraker's lane_data
    Stock = 2,      // stock Flashforge firmware: the local API's msConfig_cmd
                    // (slot material and colour only)
};

enum class Model : uint8_t { AD5X = 0, Creator5 = 1 };

constexpr size_t MAX_PRINTERS = 6;
constexpr uint16_t ZMOD_NFC_PORT = 7140;
constexpr uint16_t MOONRAKER_PORT = 7125;
constexpr uint16_t FLASHFORGE_PORT = 8898;

struct PrinterConfig {
    String name;
    String host;
    Model model = Model::AD5X;
    Method method = Method::ZmodNfc;
    uint16_t port = 0;          // 0: the method's default
    String serial;              // stock: printer serial number
    String checkCode;           // stock: the printer's check code
    String token;               // zmod_nfc: remote_token, if the plugin sets one

    uint16_t effectivePort() const;
    String baseUrl() const;
};

struct Config {
    String hostname = "zmod-nfc";
    String wifiSsid;
    String wifiPass;
    String uiPassword;          // empty: the web page is open
    std::vector<PrinterConfig> printers;
    int active = 0;             // printer scans go to
    // Basic and stock methods: send to this slot right after a scan; 0 asks
    // on the web page.
    int autoSlot = 0;
    ReaderPins reader;
    int ledPin = -1;
    bool ledActiveLow = false;
    int buzzerPin = -1;

    const PrinterConfig *activePrinter() const {
        return active >= 0 && active < (int)printers.size() ? &printers[active] : nullptr;
    }
};

const char *methodName(Method method);
const char *modelName(Model model);

void loadConfig(Config &config);
void saveConfig(const Config &config);

// secrets=false leaves passwords, check codes and tokens out (only whether
// they are set).
void configToJson(const Config &config, JsonObject out, bool secrets);
// Applies a page's settings. Secrets missing from in keep their value.
// Returns an error message, or "" on success; restart is set when the
// change needs a reboot (Wi-Fi, reader pins).
String configFromJson(Config &config, JsonObjectConst in, bool &restart);
