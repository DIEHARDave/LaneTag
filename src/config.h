// Settings, kept as JSON in NVS and edited on the web page.
#pragma once

#include <ArduinoJson.h>
#include <vector>

#include "display.h"
#include "reader.h"

// How a scan reaches a printer.
enum class Method : uint8_t {
    ZmodNfc = 0,    // Zmod + zmod_nfc plugin: the raw tag read goes through
                    // Moonraker to the plugin's endpoint, which decodes it, looks it
                    // up in Spoolman and shows its slot prompt, like a USB reader on
                    // the printer
    ZmodBasic = 1,  // Zmod without the plugin: colour and material through
                    // Moonraker (CHANGE_ZCOLOR) and Moonraker's lane_data
    Stock = 2,      // stock Flashforge firmware: the local API's msConfig_cmd
                    // (slot material and colour only)
};

enum class Model : uint8_t { AD5X = 0, Creator5 = 1 };

constexpr size_t MAX_PRINTERS = 6;
// IFS slots on the AD5X, toolheads on the Creator 5.
constexpr int SLOT_COUNT = 4;
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
    String apiKey;              // Zmod methods: Moonraker API key, when the reader
                                // is not in Moonraker's trusted_clients
    // A reader fixed to one slot (one reader per IFS slot or toolhead):
    // scans go straight into this slot, with no prompt. 0: ask.
    int slot = 0;

    uint16_t effectivePort() const;
    String baseUrl() const;
};

struct Config {
    String hostname = "zmod-nfc";
    String wifiSsid;
    String wifiPass;
    String uiPassword;          // empty: the web page is open
    // Wi-Fi transmit power in quarter dBm (the wifi_power_t values: 34 is
    // 8.5 dBm, 78 is 19.5 dBm); 0: the chip's default (15 dBm on the C3).
    int wifiPower = 0;
    std::vector<PrinterConfig> printers;
    int active = 0;             // printer scans go to
    ReaderPins reader;
    int ledPin = -1;
    bool ledActiveLow = false;
    int buzzerPin = -1;
    // Push button (to ground) that steps the active printer's slot; -1: none.
    int buttonPin = -1;
    DisplayConfig display;

    const PrinterConfig *activePrinter() const {
        return active >= 0 && active < (int)printers.size() ? &printers[active] : nullptr;
    }
};

const char *methodName(Method method);
const char *modelName(Model model);

void loadConfig(Config &config);
void saveConfig(const Config &config);

// secrets=false leaves passwords, check codes and API keys out (only whether
// they are set).
void configToJson(const Config &config, JsonObject out, bool secrets);
// Applies a page's settings. Secrets missing from in keep their value.
// Returns an error message, or "" on success; restart is set when the
// change needs a reboot (Wi-Fi, reader pins).
String configFromJson(Config &config, JsonObjectConst in, bool &restart);
