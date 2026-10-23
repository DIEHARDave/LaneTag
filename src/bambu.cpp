// Bambu Lab printers in LAN mode.
//
// The printer runs an MQTT broker on port 8883: TLS with Bambu's own
// certificate (not checked here), user "bblp", the LAN access code from the
// printer's screen as password. Commands go to device/<serial>/request and
// the printer's state comes back on device/<serial>/report.
// ams_filament_setting sets a tray as Bambu Studio does when a tray is
// edited (payload as ha-bambulab sends it); the external spool is AMS 255,
// tray 0.
//
// Reports are 10-30 KB; mqtt.h parses them straight from the TLS stream.
#include "bambu.h"

#include "mqtt.h"

namespace {

constexpr uint32_t TIMEOUT = 8000;
// How long the printer gets to show the new tray in a report before it is
// asked for a full one (P1 and A1 printers only report what changed, and
// do not like being asked for everything often).
constexpr uint32_t CHANGE_WAIT = 4000;
constexpr int EXTERNAL_AMS_ID = 255;

String upper(String text) {
    text.trim();
    text.toUpperCase();
    return text;
}

// --- Materials -------------------------------------------------------------------
//
// Bambu Studio's "Generic" presets (filament_id in BambuStudio's
// resources/profiles/BBL/filament/Generic * @base.json). The temperatures
// are only used when the tag has none.

struct BambuMaterial {
    const char *material;   // tray_type, and the tag material it matches
    const char *modifier;   // also needed in the tag's modifiers, or nullptr
    const char *preset;
    int nozzleMin, nozzleMax;
};
const BambuMaterial MATERIALS[] = {
    {"PLA", "SILK", "GFL96", 210, 230},     {"PLA", "HIGH SPEED", "GFL95", 190, 230},
    {"PLA-CF", nullptr, "GFL98", 210, 240}, {"PLA", nullptr, "GFL99", 190, 230},
    {"PETG-CF", nullptr, "GFG98", 240, 270}, {"PETG", "HF", "GFG96", 230, 260},
    {"PCTG", nullptr, "GFG97", 230, 260},   {"PETG", nullptr, "GFG99", 220, 260},
    {"ABS", nullptr, "GFB99", 240, 270},    {"ASA", nullptr, "GFB98", 240, 270},
    {"TPU", nullptr, "GFU99", 200, 240},    {"PA-CF", nullptr, "GFN98", 260, 290},
    {"PPA-CF", nullptr, "GFN97", 280, 310}, {"PPA-GF", nullptr, "GFN96", 280, 310},
    {"PA", nullptr, "GFN99", 260, 290},     {"PC", nullptr, "GFC99", 260, 290},
    {"PVA", nullptr, "GFS99", 190, 210},    {"HIPS", nullptr, "GFS98", 230, 260},
    {"BVOH", nullptr, "GFS97", 190, 210},   {"PPS-CF", nullptr, "GFT98", 300, 330},
    {"PPS", nullptr, "GFT97", 300, 330},    {"PE-CF", nullptr, "GFP98", 200, 240},
    {"PE", nullptr, "GFP99", 200, 240},     {"PP-CF", nullptr, "GFP96", 200, 240},
    {"PP-GF", nullptr, "GFP95", 200, 240},  {"PP", nullptr, "GFP97", 200, 240},
    {"EVA", nullptr, "GFR99", 190, 230},    {"PHA", nullptr, "GFR98", 190, 230},
};

// The preset for a tag's material: exact material (with modifier where the
// table wants one), then the families Bambu has a generic preset for.
const BambuMaterial *bambuMaterial(const Filament &f) {
    String material = upper(f.material), mods = upper(f.modifiers);
    if (!material.length()) material = "PLA";
    for (auto &m : MATERIALS)
        if (material == m.material && (!m.modifier || mods.indexOf(m.modifier) >= 0)) return &m;
    const char *family = material.startsWith("ASA")                              ? "ASA"
                         : material.startsWith("ABS")                            ? "ABS"
                         : material.startsWith("PET")                            ? "PETG"
                         : material.startsWith("PA") && material.endsWith("-CF") ? "PA-CF"
                         : material.startsWith("PA")                             ? "PA"
                         : material.indexOf("PLA") >= 0                          ? "PLA"
                         : material.startsWith("TPU") || material.startsWith("TPE") ? "TPU"
                                                                                    : nullptr;
    if (family)
        for (auto &m : MATERIALS)
            if (!m.modifier && !strcmp(m.material, family)) return &m;
    return nullptr;
}

// --- Session -----------------------------------------------------------------

// Logs in and subscribes to the printer's reports, with Bambu's reasons for
// failing.
bool openPrinter(Mqtt &mqtt, const PrinterConfig &printer) {
    if (!mqtt.open(printer.host, printer.effectivePort(), "bblp", printer.checkCode)) {
        if (mqtt.refused == 4 || mqtt.refused == 5) mqtt.error = "the printer refused the access code";
        else if (mqtt.error.startsWith("no TLS")) mqtt.error += " (is LAN mode on?)";
        return false;
    }
    // Bambu's broker drops the connection on a wrong serial number.
    if (!mqtt.subscribe("device/" + printer.serial + "/report")) {
        mqtt.error = "no reply to the report subscription (check the serial number)";
        return false;
    }
    return true;
}

bool sendCommand(Mqtt &mqtt, const PrinterConfig &printer, const JsonDocument &cmd) {
    return mqtt.publish("device/" + printer.serial + "/request", cmd);
}

// --- Reports -----------------------------------------------------------------

void reportFilter(JsonDocument &filter) {
    JsonObject print = filter["print"].to<JsonObject>();
    print["command"] = true;
    print["sequence_id"] = true;
    print["result"] = true;
    print["reason"] = true;
    JsonObject tray = print["ams"]["ams"][0]["tray"][0].to<JsonObject>();
    print["ams"]["ams"][0]["id"] = true;
    print["ams"]["tray_exist_bits"] = true;
    JsonObject external = print["vt_tray"].to<JsonObject>();
    for (JsonObject t : {tray, external}) {
        t["id"] = true;
        t["tray_type"] = true;
        t["tray_color"] = true;
        t["tray_info_idx"] = true;
    }
}

void pushAll(Mqtt &mqtt, const PrinterConfig &printer) {
    JsonDocument cmd;
    cmd["pushing"]["sequence_id"] = "0";
    cmd["pushing"]["command"] = "pushall";
    sendCommand(mqtt, printer, cmd);
}

// The tray behind a slot in a report, or null when the report does not have
// it.
JsonObjectConst findTray(const JsonDocument &report, const PrinterConfig &printer, int slot) {
    if (printer.unit == EXTERNAL_SPOOL) return report["print"]["vt_tray"];
    for (JsonObjectConst ams : report["print"]["ams"]["ams"].as<JsonArrayConst>()) {
        if (ams["id"].as<String>() != String(printer.unit - 1)) continue;
        for (JsonObjectConst tray : ams["tray"].as<JsonArrayConst>())
            if (tray["id"].as<String>() == String(slot - 1)) return tray;
    }
    return JsonObjectConst();
}

bool hasAms(const JsonDocument &report) { return report["print"]["ams"]["ams"].size() > 0; }

SlotInfo slotOf(const PrinterConfig &printer, int slot, JsonObjectConst tray, const JsonDocument &report) {
    SlotInfo info;
    info.slot = slot;
    info.label = slotName(printer, slot);
    info.material = tray["tray_type"] | "";
    String color = tray["tray_color"] | "";
    info.color = color.substring(0, 6);
    String bits = report["print"]["ams"]["tray_exist_bits"] | "";
    if (printer.unit != EXTERNAL_SPOOL && bits.length()) {
        int bit = (printer.unit - 1) * 4 + slot - 1;
        info.loaded = (strtoul(bits.c_str(), nullptr, 16) >> bit) & 1;
    } else {
        info.loaded = info.material.length() > 0;
    }
    return info;
}

// A full report with the AMS state (or the external spool, which any
// printer has).
bool fullReport(Mqtt &mqtt, const PrinterConfig &printer, JsonDocument &report, const JsonDocument &filter) {
    pushAll(mqtt, printer);
    uint32_t deadline = millis() + TIMEOUT;
    while (mqtt.receive(report, filter, deadline)) {
        if (printer.unit == EXTERNAL_SPOOL ? !report["print"]["vt_tray"].isNull() : hasAms(report)) return true;
    }
    return false;
}

}  // namespace

SlotList bambuSlots(const PrinterConfig &printer) {
    SlotList list;
    Mqtt mqtt;
    if (!openPrinter(mqtt, printer)) {
        list.error = mqtt.error;
        return list;
    }
    JsonDocument filter, report;
    reportFilter(filter);
    if (!fullReport(mqtt, printer, report, filter)) {
        list.error = printer.unit == EXTERNAL_SPOOL ? "the printer sent no status report"
                                                   : "the printer reported no AMS";
        return list;
    }
    for (int slot = 1; slot <= printer.slotCount(); slot++) {
        JsonObjectConst tray = findTray(report, printer, slot);
        if (tray.isNull()) {
            list.error = "the printer has no AMS " + String((char)('A' + printer.unit - 1));
            list.slots.clear();
            return list;
        }
        list.slots.push_back(slotOf(printer, slot, tray, report));
    }
    list.ok = true;
    return list;
}

PrinterReply bambuAssign(const PrinterConfig &printer, const Filament &f, int slot) {
    PrinterReply reply;
    if (slot < 1 || slot > printer.slotCount()) {
        reply.message = "no such slot";
        return reply;
    }
    if (!f.hasFilamentData()) {
        reply.message = f.spoolId ? "The tag only names Spoolman spool " + String(f.spoolId) +
                                        "; Bambu printers cannot look it up"
                                  : "No filament data on this tag";
        return reply;
    }
    const BambuMaterial *m = bambuMaterial(f);
    // Bambu's own tags name their exact preset.
    String preset = f.presetId.startsWith("GF") ? f.presetId : m ? String(m->preset) : String();
    if (!preset.length()) {
        reply.message = f.material + " has no Bambu preset; pick it in Bambu Studio or on the printer";
        return reply;
    }
    String type = m ? String(m->material) : upper(f.material);
    int nozzleMin = f.nozzleMin ? f.nozzleMin : m ? m->nozzleMin : 190;
    int nozzleMax = f.nozzleMax ? f.nozzleMax : m ? m->nozzleMax : 240;
    char color[12];
    snprintf(color, sizeof(color), "%06X%02X", (unsigned)(f.color >= 0 ? f.color & 0xFFFFFF : 0xFFFFFF), f.alpha);

    Mqtt mqtt;
    if (!openPrinter(mqtt, printer)) {
        reply.message = mqtt.error;
        return reply;
    }
    String sequence = String(millis() % 100000);
    JsonDocument cmd;
    JsonObject print = cmd["print"].to<JsonObject>();
    print["sequence_id"] = sequence;
    print["command"] = "ams_filament_setting";
    print["ams_id"] = printer.unit == EXTERNAL_SPOOL ? EXTERNAL_AMS_ID : printer.unit - 1;
    print["tray_id"] = printer.unit == EXTERNAL_SPOOL ? 0 : slot - 1;
    print["tray_info_idx"] = preset;
    print["tray_color"] = color;
    print["nozzle_temp_min"] = nozzleMin;
    print["nozzle_temp_max"] = nozzleMax;
    print["tray_type"] = type;
    if (!sendCommand(mqtt, printer, cmd)) {
        reply.message = mqtt.error;
        return reply;
    }

    // The printer echoes the command with its result, and reports the tray
    // once it has changed (X1 printers also keep sending the old tray until
    // then); without the new tray in time, ask for a full report.
    String hex = String(color).substring(0, 6);
    auto matches = [&](JsonObjectConst tray) {
        String c = tray["tray_color"] | "";
        return c.substring(0, 6).equalsIgnoreCase(hex) && preset == (tray["tray_info_idx"] | "");
    };
    JsonDocument filter, report;
    reportFilter(filter);
    String label = slotName(printer, slot);
    bool changed = false;
    uint32_t deadline = millis() + CHANGE_WAIT;
    while (mqtt.receive(report, filter, deadline)) {
        JsonObjectConst p = report["print"];
        if (p["command"] == "ams_filament_setting" && p["sequence_id"].as<String>() == sequence &&
            p["result"].is<const char *>() && !(p["result"] == "success")) {
            reply.message = "The printer refused it: " + String(p["reason"] | (p["result"] | "failed"));
            return reply;
        }
        JsonObjectConst tray = findTray(report, printer, slot);
        if (!tray.isNull() && matches(tray)) {
            changed = true;
            break;
        }
    }
    bool reported = changed;
    if (!changed) {
        reported = fullReport(mqtt, printer, report, filter) && !findTray(report, printer, slot).isNull();
        changed = reported && matches(findTray(report, printer, slot));
    }
    mqtt.close();

    if (!reported) {
        reply.ok = true;
        reply.message = "Sent " + type + " #" + hex + " to " + label + "; the printer did not report the tray back";
        return reply;
    }
    SlotInfo now = slotOf(printer, slot, findTray(report, printer, slot), report);
    if (!changed) {
        reply.message = label + " is still " + (now.material.length() ? now.material + " #" + now.color : "empty") +
                        ". The printer ignored the change: with newer firmware, turn on LAN Only mode and "
                        "Developer Mode";
        if (!now.loaded) reply.message += ", and load the spool before scanning";
        return reply;
    }
    reply.ok = true;
    reply.title = f.title();
    reply.message = label + " is now " + type + " #" + now.color + " (" + preset + ")";
    return reply;
}
