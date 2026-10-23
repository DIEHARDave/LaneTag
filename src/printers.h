// Talking to printers, one way per Method (see config.h).
#pragma once

#include <ArduinoJson.h>
#include <vector>

#include "config.h"
#include "tag.h"

struct PrinterReply {
    bool ok = false;
    String message;     // for the web page
    String outcome;     // zmod_nfc: prompt, assigned, disabled, printing, no_data
    String title;       // zmod_nfc: what the printer made of the tag
    int spoolId = 0;    // zmod_nfc: the Spoolman spool the printer found
};

struct SlotInfo {
    int slot = 0;
    String label;
    String material;
    String color;       // RRGGBB
    bool loaded = false;
};

struct SlotList {
    bool ok = false;
    String error;
    std::vector<SlotInfo> slots;
};

// zmod_nfc: hand the raw read to the plugin, which prompts on the printer,
// or with slot > 0 assigns it to that slot straight away.
PrinterReply sendScan(const PrinterConfig &printer, const Tag &tag, const String &readerName, int slot = 0);
// Put the scanned spool into a slot (1-based).
PrinterReply assignSlot(const PrinterConfig &printer, const Tag &tag, const Filament &filament,
                        int slot, const String &readerName);
// zmod_nfc: close the plugin's slot prompt.
void cancelPrompt(const PrinterConfig &printer);
SlotList listSlots(const PrinterConfig &printer);
// Klipper: remember how many slots the printer has for this reader (and
// keep the fixed slot among them). Returns whether the settings changed.
bool learnSlots(PrinterConfig &printer, const SlotList &list);
// A Test button reply from a slot list.
PrinterReply testReply(const SlotList &slots);

void slotListToJson(const SlotList &list, JsonObject out);
