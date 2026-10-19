// What the reader produced (Tag) and what the decoders understood (Filament).
//
// Mirrors zmod_nfc's zmodnfc/tag.py, so a Tag can be sent to the printer
// plugin as is and decoded there with every format it knows.
#pragma once

#include <Arduino.h>
#include <vector>

enum class TagKind : uint8_t { Unknown, Type2, MifareClassic, Type4 };

// zmod_nfc's names for the kinds ("type2", "mifare_classic", "type4").
const char *tagKindName(TagKind kind);

constexpr size_t MIFARE_BLOCK = 16;
constexpr int MAX_SECTORS = 40;

struct Tag {
    uint8_t uid[10] = {};
    uint8_t uidLen = 0;
    uint8_t atqa[2] = {};
    uint8_t sak = 0;
    TagKind kind = TagKind::Unknown;
    // Memory from the first page/block, as on the datasheet. Type 2: pages
    // 0..n. MIFARE Classic: every block; sectors that did not open and the
    // sector trailers stay zero.
    std::vector<uint8_t> memory;
    // Type 4: the NDEF file.
    std::vector<uint8_t> ndef;
    // MIFARE Classic: the name of the key family that opened each sector
    // ("Bambu", "default", ...), or nullptr when it was not read.
    const char *sectorKey[MAX_SECTORS] = {};
    // The tag left the reader before it was fully read.
    bool incomplete = false;

    String uidHex() const;
    bool sectorRead(int sector) const {
        return sector >= 0 && sector < MAX_SECTORS && sectorKey[sector] != nullptr;
    }
    int sectorsRead() const;
    String describe() const;
};

// MIFARE Classic layout: 4 blocks per sector for the first 32 sectors, 16
// after that (4K).
int mifareSectorCount(size_t memorySize);
void mifareSector(int sector, int &firstBlock, int &blockCount);

struct Filament {
    String format;          // decoder that produced it; empty: nothing decoded
    String material;        // base material: "PLA", "PETG-CF", ...
    String modifiers;       // space-separated: "Silk Matte"
    String brand;
    String name;
    int32_t color = -1;     // 0xRRGGBB, or -1
    uint8_t alpha = 0xFF;
    float diameter = 0;
    int weight = 0;         // g of filament on a full spool
    int nozzleMin = 0;
    int nozzleMax = 0;
    int bedTemp = 0;
    int spoolId = 0;        // Spoolman spool, if the tag names one
    int filamentId = 0;

    bool valid() const { return format.length() > 0; }
    bool hasFilamentData() const { return material.length() > 0 || color >= 0; }
    int nozzleTemp() const;
    String colorHex() const;     // "RRGGBB", or "" without a colour
    String title() const;        // brand, material and modifiers
};

String toHex(const uint8_t *data, size_t len);
