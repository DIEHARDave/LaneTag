// NFC Forum NDEF: finding the message in a tag's memory and splitting it
// into records. Port of FF_NFC's decoders/ndef.py.
#pragma once

#include <vector>

#include "tag.h"

struct NdefRecord {
    uint8_t tnf = 0;
    String type;
    std::vector<uint8_t> payload;

    bool isText() const { return tnf == 0x01 && type == "T"; }
    bool isUri() const { return (tnf == 0x01 && type == "U") || tnf == 0x03; }
    String mime() const;     // lower-case MIME type, or "" for other TNFs
    String text() const;     // text record content, else the payload as text
    String uri() const;
};

std::vector<NdefRecord> tagRecords(const Tag &tag);
