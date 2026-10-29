// Tag decoders: a read Tag to Filament data. Ported from FF_NFC's
// decoders (GPL-3.0, adapted there from OpenRFID).
//
// The device decodes for the stock-firmware and "Zmod basic" methods and
// for its own web page. With the FF_NFC method the printer plugin gets
// the raw read and decodes it itself, with every format it knows
// (OpenTag3D and TigerTag are only decoded there).
#pragma once

#include <ArduinoJson.h>

#include "tag.h"

Filament decodeTag(const Tag &tag);

// Material normalising, as FF_NFC's materials.normalize: base material
// in upper case ("PETG-CF"), the other words added to modifiers.
void normalizeMaterial(const String &text, String &material, String &modifiers);

int32_t parseColor(String text);

void filamentToJson(const Filament &filament, JsonObject out);
