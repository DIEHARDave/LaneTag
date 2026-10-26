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
    ZmodBasic = 1,  // Zmod without the plugin: color and material through
                    // Moonraker (CHANGE_ZCOLOR) and Moonraker's lane_data
    Stock = 2,      // stock Flashforge firmware: the local API's msConfig_cmd
                    // (slot material and color only)
    Snapmaker = 3,  // Snapmaker U1 with paxx12's Extended firmware: the read
                    // goes to the head as if its own RFID reader had read it
                    // (Moonraker's filament_detect/set, as OpenRFID does), and
                    // the firmware's SpoolLink matches the UID in Spoolman
    Bambu = 4,      // Bambu Lab in LAN mode: the printer's MQTT broker
                    // (ams_filament_setting: tray material, color and preset)
    HappyHare = 5,  // Klipper with Happy Hare: MMU_GATE_MAP through Moonraker
    Afc = 6,        // Klipper with AFC (Box Turtle, Night Owl, ...): SET_MATERIAL,
                    // SET_COLOR, SET_SPOOL_ID per lane through Moonraker
    LaneData = 7,   // any Klipper printer: Moonraker's lane_data (read by
                    // OrcaSlicer) and Spoolman's active spool
    CrealityCfs = 8,  // Creality K1 / K2 / Hi stock firmware: Creality Print's
                      // WebSocket (modifyMaterial: CFS slot material and color)
    AnycubicAce = 9,  // Anycubic Kobra 3 / S1 in LAN mode: the printer's MQTT
                      // broker (multiColorBox setInfo: ACE slot material, color)
    QidiBox = 10,     // Qidi Plus4 / Q2 / Max4 stock firmware: the Box's
                      // save_variables through Moonraker
    SnapmakerStock = 11,  // Snapmaker U1 stock firmware: SET_PRINT_FILAMENT_CONFIG
                          // through Moonraker (head type, vendor and color)
    Spoolman = 12,    // no printer: Spoolman's own API finds the spool, binds
                      // the tag to it or adds it (a reader by the shelf)
};

// Adventurer5M: the Flashforge AD5M (Adventurer 5M) on Zmod or Forge-X. Neither its stock
// firmware nor Zmod keep a filament there, so it takes the lane data method
// (Moonraker), always for its one tool. Spoolman: not a printer, a Spoolman
// server (one "slot").
enum class Model : uint8_t {
    AD5X = 0, Creator5 = 1, SnapmakerU1 = 2, Bambu = 3, Klipper = 4, Adventurer5M = 5, Creality = 6,
    Anycubic = 7, Qidi = 8, Spoolman = 9
};

// Whether a printer model takes a method: the U1, Bambu and Klipper printers
// have methods of their own, the Flashforge printers the rest.
bool methodFits(Model model, Method method);

constexpr size_t MAX_PRINTERS = 6;
// IFS slots on the AD5X, toolheads on the Creator 5 and the Snapmaker U1,
// the trays of one AMS on a Bambu printer; four gates, lanes or tools of a
// Klipper printer (PrinterConfig::unit picks which four).
constexpr int SLOT_COUNT = 4;
constexpr uint16_t MOONRAKER_PORT = 7125;
constexpr uint16_t FLASHFORGE_PORT = 8898;
constexpr uint16_t BAMBU_PORT = 8883;
constexpr uint16_t CREALITY_PORT = 9999;
// Anycubic's LAN handshake; its MQTT broker's port comes from the handshake.
constexpr uint16_t ANYCUBIC_PORT = 18910;
constexpr uint16_t SPOOLMAN_PORT = 7912;
// Bambu and Creality: PrinterConfig::unit for the external spool holder.
constexpr int EXTERNAL_SPOOL = 0;

struct PrinterConfig {
    String name;
    String host;
    Model model = Model::AD5X;
    Method method = Method::ZmodNfc;
    uint16_t port = 0;          // 0: the method's default
    String serial;              // stock, Bambu: printer serial number
    String checkCode;           // stock: the printer's check code; Bambu: its
                                // LAN access code
    String apiKey;              // Zmod methods: Moonraker API key, when the reader
                                // is not in Moonraker's trusted_clients; Spoolman:
                                // sent as X-Api-Key (for a proxy in front of it)
    // Spoolman: add spools for tags it does not know, and the location to
    // give scanned spools ("" leaves it).
    bool spoolmanCreate = true;
    String location;
    // A reader fixed to one slot (one reader per IFS slot or toolhead):
    // scans go straight into this slot, with no prompt. 0: ask.
    int slot = 0;
    // Bambu: the AMS unit (1-4, A-D on the printer) whose trays are slots
    // 1-4, or EXTERNAL_SPOOL for the external spool (one slot). Creality: the
    // CFS (1-4) or EXTERNAL_SPOOL, likewise. Anycubic: the ACE (1-4). Qidi: the Box (1-4). Klipper: which
    // four gates, lanes or tools are slots 1-4 (1: the first four, 2: the
    // next four, ...).
    int unit = 1;
    // Klipper: the 0-based gate/lane/tool index behind a slot.
    int slotIndex(int slot) const { return (unit - 1) * SLOT_COUNT + slot - 1; }
    // Klipper: how many of the reader's four slots the printer has (its
    // tools, or the gates/lanes in the reader's group), learned whenever the
    // slots are listed; 0: not known yet, all four are offered.
    int slotsFound = 0;

    // Slots this reader can fill: 1 for a Bambu external spool, what a
    // Klipper printer has, else SLOT_COUNT.
    int slotCount() const {
        if (((model == Model::Bambu || model == Model::Creality) && unit == EXTERNAL_SPOOL) ||
            model == Model::Adventurer5M || model == Model::Spoolman)
            return 1;
        if (model == Model::Klipper && slotsFound > 0) return slotsFound;
        return SLOT_COUNT;
    }
    // Whether a scan can wait for its slot to be picked. The U1 always has a
    // fixed head: nothing there can ask, and a head's own reader works the
    // same way. A single slot needs no asking either.
    bool canAsk() const { return model != Model::SnapmakerU1 && slotCount() > 1; }
    uint16_t effectivePort() const;
    String baseUrl() const;
};

struct Config {
    String hostname = "lanetag";
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
// "Ask", "Slot 1".."Slot 4" (AD5X IFS), "T0".."T3" (Creator 5 toolheads) or
// "Head 1".."Head 4" (Snapmaker U1), "A1".."D4" or "Ext" (Bambu AMS trays
// as the printer names them, external spool), "Gate 0".. (Happy Hare),
// "lane1".. (AFC's default lane names), "T0".. (lane data, and the
// AD5M's only tool).
String slotName(const PrinterConfig &printer, int slot);
// As short as fits a touch screen button: "T0", "A1", "Ext", "3", ...
String slotShortName(const PrinterConfig &printer, int slot);
// Whether a tag that only names a Spoolman spool can do anything with this
// printer's method (the printer looks the spool up itself).
bool takesSpoolOnlyTags(Method method);

void loadConfig(Config &config);
void saveConfig(const Config &config);

// secrets=false leaves passwords, check codes and API keys out (only whether
// they are set).
void configToJson(const Config &config, JsonObject out, bool secrets);
// Applies a page's settings. Secrets missing from in keep their value.
// Returns an error message, or "" on success; restart is set when the
// change needs a reboot (Wi-Fi, reader pins).
String configFromJson(Config &config, JsonObjectConst in, bool &restart);
