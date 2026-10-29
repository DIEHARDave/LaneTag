// Spoolman as a target of its own: a reader by the shelf (or anywhere) that
// finds a scanned spool in Spoolman, binds the tag to it, or adds it.
//
// Same rules as FF_NFC's spoolman.py, so tags bound here are found by the
// printers too: a spool ID on the tag, else the tag's UID in the spool's
// "card_uids" extra field (or nfc2klipper's "nfc_id"), else the one spool
// with the tag's vendor, material and color (the UID is then bound to it).
// Spoolman 0.27+ also links tags itself (spool and filament tags) and takes
// scans from readers (/tag/scan), which its web page reacts to; those are
// used and kept in step too.
#pragma once

#include "config.h"
#include "printers.h"
#include "tag.h"

PrinterReply spoolmanScan(const PrinterConfig &server, const Tag &tag, const Filament &f, const String &readerName);

// What Spoolman has the tag linked to (0: nothing); ok: Spoolman took the
// scan (0.27 or newer).
struct TagMatch {
    bool ok = false;
    int spoolId = 0;
    int filamentId = 0;
    String error;
};
// "http://host:port" from "host", "host:port" or a URL ("" for "").
String spoolmanBase(String address);
// Report a scan to Spoolman's tag scanning: the reader shows up among its
// recent readers, and a browser following it opens the spool.
TagMatch reportScan(const String &base, const String &apiKey, const Tag &tag, const Filament &f,
                    const String &readerName);
// The spool a tag's UID is linked to (Spoolman's own tag links, Card UIDs
// or nfc_id), 0 for none; false, with error, when Spoolman can't be asked.
bool spoolForUid(const String &base, const String &apiKey, const String &tagUid, int &spoolId, String &error);
// Whether Spoolman answers, as a one-slot list for the page.
SlotList spoolmanStatus(const PrinterConfig &server);
