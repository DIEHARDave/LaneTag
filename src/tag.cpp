#include "tag.h"

const char *tagKindName(TagKind kind) {
    switch (kind) {
    case TagKind::Type2: return "type2";
    case TagKind::MifareClassic: return "mifare_classic";
    case TagKind::Type4: return "type4";
    default: return "unknown";
    }
}

String toHex(const uint8_t *data, size_t len) {
    static const char digits[] = "0123456789ABCDEF";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0F];
    }
    return out;
}

String Tag::uidHex() const { return toHex(uid, uidLen); }

int Tag::sectorsRead() const {
    int count = 0;
    for (int i = 0; i < MAX_SECTORS; i++)
        if (sectorKey[i]) count++;
    return count;
}

String Tag::describe() const {
    String text = String(tagKindName(kind)) + ", UID " + uidHex();
    char sakText[12];
    snprintf(sakText, sizeof(sakText), ", SAK %02X", sak);
    text += sakText;
    if (kind == TagKind::MifareClassic)
        text += ", " + String(sectorsRead()) + "/" + String(mifareSectorCount(memory.size())) + " sectors";
    else if (kind == TagKind::Type4)
        text += ", NDEF " + String(ndef.size()) + " bytes";
    else
        text += ", " + String(memory.size()) + " bytes";
    return text;
}

int mifareSectorCount(size_t memorySize) {
    int blocks = memorySize / MIFARE_BLOCK;
    if (blocks <= 128) return blocks / 4;
    return 32 + (blocks - 128) / 16;
}

void mifareSector(int sector, int &firstBlock, int &blockCount) {
    if (sector < 32) {
        firstBlock = sector * 4;
        blockCount = 4;
    } else {
        firstBlock = 128 + (sector - 32) * 16;
        blockCount = 16;
    }
}

int Filament::nozzleTemp() const {
    if (nozzleMin && nozzleMax) return (nozzleMin + nozzleMax + 1) / 2;
    return nozzleMax ? nozzleMax : nozzleMin;
}

String Filament::colorHex() const {
    if (color < 0) return "";
    char text[8];
    snprintf(text, sizeof(text), "%06X", (unsigned)(color & 0xFFFFFF));
    return text;
}

String Filament::title() const {
    String words[3] = {brand, material, modifiers};
    String title;
    for (auto &word : words) {
        if (!word.length()) continue;
        if (title.length()) title += ' ';
        title += word;
    }
    if (name.length()) {
        String lowerTitle = title, lowerName = name;
        lowerTitle.toLowerCase();
        lowerName.toLowerCase();
        if (lowerTitle.indexOf(lowerName) < 0) {
            if (title.length()) title += ' ';
            title += name;
        }
    }
    return title.length() ? title : format;
}
