// Talking to printers, one way per Method (see config.h).
#pragma once

#include <ArduinoJson.h>
#include <vector>

#include "config.h"
#include "tag.h"

struct PrinterReply {
    bool ok = false;
    String message;     // for the web page
    String outcome;     // FF_NFC: prompt, assigned, disabled, printing, no_data
    String title;       // FF_NFC: what the printer made of the tag
    int spoolId = 0;    // FF_NFC: the Spoolman spool the printer found
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
    String note;        // the Test button's reply instead of the slot count
    std::vector<SlotInfo> slots;
};

// FF_NFC: hand the raw read to the plugin, which prompts on the printer,
// or with slot > 0 assigns it to that slot straight away.
PrinterReply sendScan(const PrinterConfig &printer, const Tag &tag, const String &readerName, int slot = 0);
// Put the scanned spool into a slot (1-based).
PrinterReply assignSlot(const PrinterConfig &printer, const Tag &tag, const Filament &filament,
                        int slot, const String &readerName);
// AFC: give the tag's Spoolman spool to the next lane loaded. False, with
// the reason in reply.message and nothing sent, when the tag names no spool
// or AFC has no Spoolman: the scan then needs a lane.
bool afcNextLane(const PrinterConfig &printer, const Filament &f, PrinterReply &reply);
// Happy Hare, AFC and lane data: the spool the printer's Spoolman has the
// tag's UID linked to, for tags that name no spool themselves (0: none, or
// no Spoolman; note says why when it matters).
int linkedSpool(const PrinterConfig &printer, const Tag &tag, String &note);
// FF_NFC: close the plugin's slot prompt.
void cancelPrompt(const PrinterConfig &printer);
SlotList listSlots(const PrinterConfig &printer);
// Klipper: remember how many slots the printer has for this reader (and
// keep the fixed slot among them). Returns whether the settings changed.
bool learnSlots(PrinterConfig &printer, const SlotList &list);
// A Test button reply from a slot list.
PrinterReply testReply(const SlotList &slots);

void slotListToJson(const SlotList &list, JsonObject out);
