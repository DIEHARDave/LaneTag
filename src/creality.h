// Creality K1 / K2 / Hi printers on stock firmware, through the WebSocket
// Creality Print uses (port 9999).
#pragma once

#include "printers.h"

// Set a CFS slot (slot 1-4 = A-D of CFS printer.unit) or the external spool:
// material, color, temperatures and the matching entry of the printer's
// material library.
PrinterReply crealityAssign(const PrinterConfig &printer, const Filament &filament, int slot);
// The slots of CFS printer.unit, or the external spool.
SlotList crealitySlots(const PrinterConfig &printer);
