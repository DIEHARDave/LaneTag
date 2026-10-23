// Bambu Lab printers in LAN mode, through the printer's own MQTT broker.
#pragma once

#include "printers.h"

// Set an AMS tray (slot 1-4 of printer.unit) or the external spool: material,
// color, temperatures and the filament preset Bambu Studio shows.
PrinterReply bambuAssign(const PrinterConfig &printer, const Filament &filament, int slot);
// The trays of printer.unit, or the external spool.
SlotList bambuSlots(const PrinterConfig &printer);
