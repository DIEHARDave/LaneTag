// OpenPrintTag (Prusa, https://specs.openprinttag.org): the CBOR maps in an
// "application/vnd.openprinttag" NDEF record. Port of zmod_nfc's
// decoders/cbor.py and decode_openprinttag. Plain C++, so it can be tested
// off the device.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct OpenPrintTag {
    std::string material;   // material name, or the type from the spec's list
    std::string brand;
    std::string name;
    int32_t color = -1;     // 0xRRGGBB, or -1
    uint8_t alpha = 0xFF;
    float diameter = 1.75f;
    int weight = 0;         // g of filament left (full weight minus consumed)
    int nozzleMin = 0;
    int nozzleMax = 0;
    int bedTemp = 0;
};

// False when the payload holds no OpenPrintTag main section.
bool parseOpenPrintTag(const uint8_t *payload, size_t len, OpenPrintTag &out);
