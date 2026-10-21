// Tag reading from zmod_nfc's readers/base.py.
#include "reader.h"

#include "keys.h"

namespace {

constexpr uint8_t MIFARE_READ = 0x30;
constexpr uint8_t MIFARE_AUTH_A = 0x60;
constexpr uint8_t MIFARE_AUTH_B = 0x61;

constexpr int TYPE2_PAGES_WITHOUT_CC = 45;
constexpr int TYPE2_MAX_PAGES = 231;           // NTAG216
// Failed page reads per tag that are tried again before the read ends.
constexpr int TYPE2_RETRIES = 3;
// Polls in a row that must miss a card before it counts as removed.
constexpr int REMOVAL_MISSES = 2;
constexpr uint32_t REINIT_INTERVAL = 5000;
// Type 4 read size; the I2C transfer must hold it.
constexpr size_t TYPE4_CHUNK = 96;
// ISO 15693 tags that do not report their size: ICODE SLIX2 (320 bytes).
constexpr int TYPE5_MAX_BLOCKS = 80;
// Bytes per multiple block read.
constexpr int TYPE5_READ_MAX = 64;

constexpr uint8_t ISO15693_ADDRESSED = 0x22;   // addressed, high data rate
constexpr uint8_t ISO15693_READ_SINGLE = 0x20;
constexpr uint8_t ISO15693_READ_MULTIPLE = 0x23;
constexpr uint8_t ISO15693_SYSTEM_INFO = 0x2B;

// Offset just past the NDEF TLV of a Type 2 memory image, or -1.
int ndefEnd(const std::vector<uint8_t> &memory) {
    size_t pos = 16;
    while (pos + 1 < memory.size() && memory[pos] == 0x00) pos++;
    if (pos + 1 >= memory.size() || memory[pos] != 0x03) return -1;
    size_t length = memory[pos + 1];
    if (length == 0xFF) {
        if (pos + 4 > memory.size()) return -1;
        return pos + 4 + ((memory[pos + 2] << 8) | memory[pos + 3]);
    }
    return pos + 2 + length;
}

}  // namespace

void TagReader::begin(const ReaderPins &pins) {
    pins_ = pins;
    if (pins.chip == ReaderChip::RC522) {
        rc522_.attach(SPI, pins.sck, pins.miso, pins.mosi, pins.ss, pins.rst);
        chip_ = &rc522_;
    } else if (pins.chip == ReaderChip::PN5180) {
        pn5180_.attach(SPI, pins.sck, pins.miso, pins.mosi, pins.ss, pins.busy, pins.rst);
        chip_ = &pn5180_;
    } else {
        if (pins.bus == Pn532Bus::I2C)
            pn532_.attachI2C(Wire, pins.sda, pins.scl);
        else
            pn532_.attachUart(Serial1, pins.rx, pins.tx);
        chip_ = &pn532_;
    }
    nextInit_ = 0;
}

String TagReader::describe() const {
    String name = chip_->chipName();
    if (!chip_->ready()) return name + " not found (" + chip_->busName() + ")";
    return name + " v" + chip_->firmware() + " (" + chip_->busName() + ")";
}

bool TagReader::poll(Tag &tag) {
    if (!chip_->ready()) {
        if ((int32_t)(millis() - nextInit_) < 0) return false;
        nextInit_ = millis() + REINIT_INTERVAL;
        // A line held low makes every I2C transfer (and the bus scan below)
        // wait for its timeout, which would stall the web page for minutes.
        const char *stuck = pins_.chip == ReaderChip::PN532 ? pn532_.stuckLine() : nullptr;
        if (stuck) {
            int pin = strcmp(stuck, "SCL") == 0 ? pins_.scl : pins_.sda;
            String error = String("I2C ") + stuck + " (GPIO " + String(pin) +
                           ") is held low, by the reader or a wire touching ground; check the wiring and "
                           "the mode switches, or use HSU (UART) mode";
            if (error != error_) log_w("%s", error.c_str());
            error_ = error;
            return false;
        }
        if (!chip_->init()) {
            String error = String("no ") + chip_->chipName() + " answering on " + chip_->busName();
            if (pins_.chip == ReaderChip::RC522) {
                error += " (SCK " + String(pins_.sck) + ", MISO " + String(pins_.miso) + ", MOSI " +
                         String(pins_.mosi) + ", SDA/SS " + String(pins_.ss) + ")";
                error += "; check the wiring and that it has 3.3 V";
            } else if (pins_.chip == ReaderChip::PN5180) {
                error += " (SCK " + String(pins_.sck) + ", MISO " + String(pins_.miso) + ", MOSI " +
                         String(pins_.mosi) + ", NSS " + String(pins_.ss) + ", BUSY " + String(pins_.busy) + ")";
                error += "; check the wiring and that it has 5 V and 3.3 V";
            } else {
                if (pins_.bus == Pn532Bus::I2C) error += " (" + scanI2C() + ")";
                error += "; check the wiring and the mode switches";
            }
            if (error != error_) log_w("%s", error.c_str());
            error_ = error;
            return false;
        }
        error_ = "";
        log_i("%s firmware %s", chip_->chipName(), chip_->firmware().c_str());
    }
    NfcTarget target;
    if (!findCard(target)) {
        if (chip_->failures() >= 5) {
            chip_->end();
            begin(pins_);
            error_ = String(chip_->chipName()) + " stopped answering";
            return false;
        }
        if (++misses_ >= REMOVAL_MISSES) presentLen_ = 0;
        return false;
    }
    misses_ = 0;
    if (presentLen_ == target.uidLen && memcmp(present_, target.uid, presentLen_) == 0) {
        chip_->release();
        return false;
    }
    tag = Tag();
    memcpy(tag.uid, target.uid, target.uidLen);
    tag.uidLen = target.uidLen;
    memcpy(tag.atqa, target.atqa, 2);
    tag.sak = target.sak;

    uint8_t sak = target.sak;
    bool ok;
    if (target.iso15693)
        ok = readType5(target, tag);
    else if (sak & 0x08 || sak == 0x01 || sak == 0x09 || sak == 0x10 || sak == 0x11 || sak == 0x18 ||
        sak == 0x19 || sak == 0x28 || sak == 0x38)
        ok = readMifareClassic(target, tag);
    else if (sak & 0x20)
        ok = readType4(target, tag);
    else
        ok = readType2(target, tag);
    chip_->release();
    if (ok && !tag.incomplete) {
        memcpy(present_, target.uid, target.uidLen);
        presentLen_ = target.uidLen;
    }
    return ok;
}

String TagReader::scanI2C() {
    String found;
    for (uint8_t address = 0x08; address < 0x78; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) continue;
        char text[8];
        snprintf(text, sizeof(text), "0x%02X", address);
        found += (found.length() ? ", " : "") + String(text);
    }
    if (!found.length()) return "nothing answers on SDA " + String(pins_.sda) + " / SCL " + String(pins_.scl);
    return "I2C devices at " + found + ", the PN532 is 0x24";
}

// An ISO 14443A card, or on chips that have it an ISO 15693 one; the kind
// found last is looked for first, so a tag left on the reader is found
// without switching the field over.
bool TagReader::findCard(NfcTarget &target) {
    if (!chip_->hasIso15693()) return chip_->listTarget(target);
    bool found;
    if (vicinityFirst_)
        found = chip_->inventory15693(target) || chip_->listTarget(target);
    else
        found = chip_->listTarget(target) || chip_->inventory15693(target);
    if (found) vicinityFirst_ = target.iso15693;
    return found;
}

bool TagReader::reselect(const NfcTarget &target) {
    NfcTarget again;
    return chip_->listTarget(again) && again.sameCard(target);
}

bool TagReader::readType2(const NfcTarget &target, Tag &tag) {
    tag.kind = TagKind::Type2;
    uint8_t buf[20];
    const uint8_t first[] = {MIFARE_READ, 0};
    if (chip_->exchange(first, 2, buf, 16) < 16) return false;
    tag.memory.assign(buf, buf + 16);
    int pages = TYPE2_PAGES_WITHOUT_CC;
    if (buf[12] == 0xE1 && buf[14]) pages = min(4 + buf[14] * 8 / 4, TYPE2_MAX_PAGES);
    tag.memory.reserve(pages * 4 + 16);
    int retries = 0;
    for (int page = 4; page < pages; page += 4) {
        const uint8_t cmd[] = {MIFARE_READ, (uint8_t)page};
        if (chip_->exchange(cmd, 2, buf, 16) < 16) {
            // A card that went away mid-read; a short tag just ends here.
            if (!reselect(target)) {
                tag.incomplete = true;
                break;
            }
            // Still there: a one-off transfer error, so read the page again.
            if (++retries > TYPE2_RETRIES) break;
            page -= 4;
            continue;
        }
        // MIFARE Ultralight wraps around past its last page.
        if (page >= 16 && memcmp(buf, tag.memory.data(), 16) == 0) break;
        tag.memory.insert(tag.memory.end(), buf, buf + 16);
        // Stop once a complete NDEF message is in; the first 176 bytes are
        // always read for formats with fixed-offset data.
        int end = ndefEnd(tag.memory);
        if (end >= 0 && tag.memory.size() >= (size_t)max(end, 176)) break;
    }
    if (tag.memory.size() > (size_t)pages * 4) tag.memory.resize(pages * 4);
    return true;
}

// 1: opened, 0: refused (card reselected), -1: card gone.
int TagReader::auth(const NfcTarget &target, int block, const uint8_t *key, bool keyB) {
    uint8_t cmd[12], resp[4];
    cmd[0] = keyB ? MIFARE_AUTH_B : MIFARE_AUTH_A;
    cmd[1] = block;
    memcpy(cmd + 2, key, 6);
    memcpy(cmd + 8, target.uid + target.uidLen - 4, 4);
    if (chip_->exchange(cmd, sizeof(cmd), resp, sizeof(resp)) >= 0) return 1;
    // A failed authentication halts the card.
    return reselect(target) ? 0 : -1;
}

bool TagReader::readMifareClassic(const NfcTarget &target, Tag &tag) {
    tag.kind = TagKind::MifareClassic;
    size_t size = 1024;
    if (target.sak == 0x18 || target.sak == 0x38) size = 4096;
    else if (target.sak == 0x09) size = 320;     // MIFARE Mini
    tag.memory.assign(size, 0);

    KeySet *vendor = new KeySet[VENDOR_KEYSETS];
    vendorKeysets(target.uid, vendor);
    // Every failed authentication halts the card and costs a reselect, so
    // find which family's keys open sector 1 (where every supported vendor
    // format keeps data) and try that family first everywhere.
    int family = -1;
    for (int i = 0; i < VENDOR_KEYSETS && family < 0; i++) {
        for (int keyB = 0; keyB < 2 && family < 0; keyB++) {
            bool has = keyB ? vendor[i].hasB[1] : vendor[i].hasA[1];
            if (!has) continue;
            int result = auth(target, 4, keyB ? vendor[i].b[1] : vendor[i].a[1], keyB);
            if (result < 0) {
                delete[] vendor;
                tag.incomplete = true;
                return true;
            }
            if (result > 0) family = i;
        }
    }

    int sectors = mifareSectorCount(size);
    uint8_t buf[20];
    for (int sector = 0; sector < sectors; sector++) {
        int firstBlock, count;
        mifareSector(sector, firstBlock, count);
        const char *opened = nullptr;
        bool gone = false;
        // The vendor family's keys for this sector, then the defaults.
        struct Candidate { const char *name; const uint8_t *key; bool keyB; };
        Candidate candidates[8];
        int n = 0;
        if (family >= 0 && sector < KEYSET_SECTORS) {
            KeySet &set = vendor[family];
            if (set.hasA[sector]) candidates[n++] = {set.name, set.a[sector], false};
            if (set.hasB[sector]) candidates[n++] = {set.name, set.b[sector], true};
        }
        for (int k = 0; k < 4; k++) candidates[n++] = {DEFAULT_KEYSET_NAME, DEFAULT_KEYS[k], false};
        candidates[n++] = {DEFAULT_KEYSET_NAME, DEFAULT_KEYS[0], true};
        for (int c = 0; c < n; c++) {
            int result = auth(target, firstBlock, candidates[c].key, candidates[c].keyB);
            if (result > 0) {
                opened = candidates[c].name;
                break;
            }
            if (result < 0) {
                gone = true;
                break;
            }
        }
        if (gone) {
            tag.incomplete = true;
            break;
        }
        if (!opened) continue;
        // The sector trailer holds only keys and access bits; skip it.
        bool ok = true;
        for (int block = firstBlock; block < firstBlock + count - 1; block++) {
            const uint8_t cmd[] = {MIFARE_READ, (uint8_t)block};
            if (chip_->exchange(cmd, 2, buf, 16) < 16) {
                ok = false;
                break;
            }
            memcpy(tag.memory.data() + block * MIFARE_BLOCK, buf, MIFARE_BLOCK);
        }
        if (!ok) {
            memset(tag.memory.data() + firstBlock * MIFARE_BLOCK, 0, (count - 1) * MIFARE_BLOCK);
            if (!reselect(target)) {
                tag.incomplete = true;
                break;
            }
            continue;
        }
        tag.sectorKey[sector] = opened;
    }
    delete[] vendor;
    return true;
}

int TagReader::apdu(const uint8_t *cmd, size_t len, uint8_t *out, size_t expect) {
    int n = chip_->exchange(cmd, len, out, expect, 2000);
    if (n < 2 || out[n - 2] != 0x90 || out[n - 1] != 0x00) return -1;
    return n - 2;
}

// NDEF file of an NFC Forum Type 4 tag (ISO-DEP, e.g. DESFire).
bool TagReader::readType4(const NfcTarget &target, Tag &tag) {
    tag.kind = TagKind::Type4;
    uint8_t resp[TYPE4_CHUNK + 4];
    const uint8_t selectApp[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00};
    const uint8_t selectCc[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, 0xE1, 0x03};
    const uint8_t readCc[] = {0x00, 0xB0, 0x00, 0x00, 0x0F};
    if (apdu(selectApp, sizeof(selectApp), resp, 4) < 0) return true;
    if (apdu(selectCc, sizeof(selectCc), resp, 4) < 0) return true;
    if (apdu(readCc, sizeof(readCc), resp, 17) < 15) return true;
    size_t maxRead = min<size_t>(max<size_t>(1, (resp[3] << 8) | resp[4]), TYPE4_CHUNK);
    uint8_t selectFile[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, resp[9], resp[10]};
    if (apdu(selectFile, sizeof(selectFile), resp, 4) < 0) return true;
    const uint8_t readLength[] = {0x00, 0xB0, 0x00, 0x00, 0x02};
    if (apdu(readLength, sizeof(readLength), resp, 4) < 2) return true;
    size_t length = min<size_t>((resp[0] << 8) | resp[1], 4096);
    size_t offset = 2;
    while (tag.ndef.size() < length) {
        uint8_t chunk = min(maxRead, length - tag.ndef.size());
        const uint8_t read[] = {0x00, 0xB0, (uint8_t)(offset >> 8), (uint8_t)offset, chunk};
        int n = apdu(read, sizeof(read), resp, chunk + 2);
        if (n <= 0) break;
        tag.ndef.insert(tag.ndef.end(), resp, resp + n);
        offset += n;
    }
    return true;
}

// Every block of an ISO 15693 tag, as zmod_nfc's read_type5. Requests are
// addressed, so the tag never needs selecting again.
bool TagReader::readType5(const NfcTarget &target, Tag &tag) {
    tag.kind = TagKind::Type5;
    uint8_t request[12] = {ISO15693_ADDRESSED};
    for (int i = 0; i < 8; i++) request[2 + i] = target.uid[7 - i];   // LSB first
    uint8_t resp[TYPE5_READ_MAX + 8];

    int blocks = TYPE5_MAX_BLOCKS, blockSize = 4;
    request[1] = ISO15693_SYSTEM_INFO;
    int n = chip_->exchange15693(request, 10, resp, sizeof(resp));
    // Info flags, UID, then DSFID, AFI and memory size when flagged.
    if (n >= 9) {
        uint8_t info = resp[0];
        int pos = 9 + ((info & 0x01) ? 1 : 0) + ((info & 0x02) ? 1 : 0);
        if ((info & 0x04) && n >= pos + 2) {
            blocks = resp[pos] + 1;
            blockSize = (resp[pos + 1] & 0x1F) + 1;
        }
    }
    tag.blockSize = blockSize;
    tag.memory.reserve(blocks * blockSize);
    int step = constrain(TYPE5_READ_MAX / blockSize, 1, 8);
    for (int block = 0; block < blocks;) {
        int count = min(step, blocks - block);
        if (count == 1) {
            request[1] = ISO15693_READ_SINGLE;
            request[10] = block;
            n = chip_->exchange15693(request, 11, resp, sizeof(resp));
        } else {
            request[1] = ISO15693_READ_MULTIPLE;
            request[10] = block;
            request[11] = count - 1;
            n = chip_->exchange15693(request, 12, resp, sizeof(resp));
        }
        if (n < count * blockSize) {
            // Reading multiple blocks is optional in ISO 15693.
            if (count > 1) {
                step = 1;
                continue;
            }
            NfcTarget again;
            if (!chip_->inventory15693(again) || !again.sameCard(target)) tag.incomplete = true;
            break;
        }
        tag.memory.insert(tag.memory.end(), resp, resp + count * blockSize);
        block += count;
    }
    return true;
}
