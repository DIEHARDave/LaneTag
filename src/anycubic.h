// Anycubic Kobra printers (Kobra 3 / S1 generation) with an ACE, in LAN mode.
#pragma once

#include "printers.h"

// Set an ACE slot (slot 1-4 of ACE printer.unit): material and color.
PrinterReply anycubicAssign(const PrinterConfig &printer, const Filament &filament, int slot);
// The slots of ACE printer.unit.
SlotList anycubicSlots(const PrinterConfig &printer);
