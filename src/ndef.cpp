#include "ndef.h"

namespace {

const char *const URI_PREFIXES[] = {
    "", "http://www.", "https://www.", "http://", "https://", "tel:",
    "mailto:", "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://",
    "sftp://", "smb://", "nfs://", "ftp://", "dav://", "news:", "telnet://",
    "imap:", "rtsp://", "urn:", "pop:", "sip:", "sips:", "tftp:", "btspp://",
    "btl2cap://", "btgoep://", "tcpobex://", "irdaobex://", "file://",
    "urn:epc:id:", "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:", "urn:epc:",
    "urn:nfc:",
};
constexpr uint16_t MAD_NDEF_AID = 0x03E1;

String bytesToString(const uint8_t *data, size_t len) {
    String text;
    text.reserve(len);
    for (size_t i = 0; i < len; i++) text += (char)data[i];
    return text;
}

// Value of the first NDEF TLV (type 0x03) in a TLV area.
std::vector<uint8_t> findTlvMessage(const uint8_t *data, size_t size, size_t pos) {
    while (pos < size) {
        uint8_t tlv = data[pos];
        if (tlv == 0x00) {                 // NULL TLV: no length
            pos++;
            continue;
        }
        if (tlv == 0xFE || pos + 1 >= size) break;
        size_t length = data[pos + 1];
        pos += 2;
        if (length == 0xFF) {
            if (pos + 2 > size) break;
            length = (data[pos] << 8) | data[pos + 1];
            pos += 2;
        }
        if (tlv == 0x03) {
            size_t end = min(pos + length, size);
            return std::vector<uint8_t>(data + pos, data + end);
        }
        pos += length;
    }
    return {};
}

// MIFARE Classic: the MAD in sector 0 lists the NDEF sectors (AID 0x03E1);
// their data blocks form one TLV area.
std::vector<uint8_t> mifareMessage(const Tag &tag) {
    if (!tag.sectorRead(0) || tag.memory.size() < 64) return {};
    const uint8_t *mad = tag.memory.data() + MIFARE_BLOCK;
    std::vector<uint8_t> area;
    int sectors = min(16, mifareSectorCount(tag.memory.size()));
    for (int sector = 1; sector < sectors; sector++) {
        uint16_t aid = mad[2 + (sector - 1) * 2] | (mad[3 + (sector - 1) * 2] << 8);
        if (aid != MAD_NDEF_AID) {
            if (!area.empty()) break;       // NDEF sectors are contiguous
            continue;
        }
        if (!tag.sectorRead(sector)) break;
        int first, count;
        mifareSector(sector, first, count);
        const uint8_t *start = tag.memory.data() + first * MIFARE_BLOCK;
        area.insert(area.end(), start, start + (count - 1) * MIFARE_BLOCK);
    }
    if (area.empty()) return {};
    return findTlvMessage(area.data(), area.size(), 0);
}

std::vector<uint8_t> tagMessage(const Tag &tag) {
    switch (tag.kind) {
    case TagKind::Type4:
        return tag.ndef;
    case TagKind::Type2:
        if (tag.memory.size() > 16 && tag.memory[12] == 0xE1)
            return findTlvMessage(tag.memory.data(), tag.memory.size(), 16);
        return {};
    case TagKind::MifareClassic:
        return mifareMessage(tag);
    case TagKind::Type5:
        // 4 byte CC (E1/E2 ...), or 8 bytes when its size byte is 0.
        if (tag.memory.size() > 8 && (tag.memory[0] == 0xE1 || tag.memory[0] == 0xE2))
            return findTlvMessage(tag.memory.data(), tag.memory.size(), tag.memory[2] == 0 ? 8 : 4);
        return {};
    default:
        return {};
    }
}

}  // namespace

String NdefRecord::mime() const {
    if (tnf != 0x02) return "";
    String m = type;
    m.toLowerCase();
    return m;
}

String NdefRecord::text() const {
    if (isText() && !payload.empty()) {
        uint8_t status = payload[0];
        size_t start = 1 + (status & 0x3F);
        if (status & 0x80 || start > payload.size()) return "";     // UTF-16: not supported
        return bytesToString(payload.data() + start, payload.size() - start);
    }
    return bytesToString(payload.data(), payload.size());
}

String NdefRecord::uri() const {
    if (tnf == 0x03) return type;
    if (payload.empty()) return "";
    uint8_t code = payload[0];
    const char *prefix = code < sizeof(URI_PREFIXES) / sizeof(URI_PREFIXES[0]) ? URI_PREFIXES[code] : "";
    return String(prefix) + bytesToString(payload.data() + 1, payload.size() - 1);
}

std::vector<NdefRecord> tagRecords(const Tag &tag) {
    std::vector<NdefRecord> records;
    std::vector<uint8_t> message = tagMessage(tag);
    const uint8_t *m = message.data();
    size_t size = message.size(), pos = 0;
    bool chunking = false;
    while (pos + 3 <= size) {
        uint8_t header = m[pos];
        uint8_t tnf = header & 0x07;
        bool shortRecord = header & 0x10, hasId = header & 0x08;
        bool chunked = header & 0x20, last = header & 0x40;
        size_t typeLen = m[pos + 1];
        pos += 2;
        size_t payloadLen;
        if (shortRecord) {
            payloadLen = m[pos++];
        } else {
            if (pos + 4 > size) break;
            payloadLen = ((size_t)m[pos] << 24) | (m[pos + 1] << 16) | (m[pos + 2] << 8) | m[pos + 3];
            pos += 4;
        }
        size_t idLen = 0;
        if (hasId) {
            if (pos >= size) break;
            idLen = m[pos++];
        }
        size_t end = pos + typeLen + idLen + payloadLen;
        if (end > size) break;
        const uint8_t *payload = m + pos + typeLen + idLen;
        if (chunking) {
            // Continuation chunk (TNF 0x06).
            auto &record = records.back();
            record.payload.insert(record.payload.end(), payload, m + end);
            chunking = chunked;
        } else {
            NdefRecord record;
            record.tnf = tnf;
            record.type = bytesToString(m + pos, typeLen);
            record.payload.assign(payload, m + end);
            records.push_back(record);
            chunking = chunked;
        }
        pos = end;
        if (last) break;
    }
    return records;
}
