// Spoolman as a target of its own: a reader by the shelf (or anywhere) that
// finds a scanned spool in Spoolman, binds the tag to it, or adds it.
//
// Same rules as zmod_nfc's spoolman.py, so tags bound here are found by the
// printers too: a spool ID on the tag, else the tag's UID in the spool's
// "card_uids" extra field (or nfc2klipper's "nfc_id"), else the one spool
// with the tag's vendor, material and color (the UID is then bound to it).
#pragma once

#include "config.h"
#include "printers.h"
#include "tag.h"

PrinterReply spoolmanScan(const PrinterConfig &server, const Tag &tag, const Filament &f);
// Whether Spoolman answers, as a one-slot list for the page.
SlotList spoolmanStatus(const PrinterConfig &server);
