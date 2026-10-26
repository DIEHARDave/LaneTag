// Turns a PN532, RC522 or PN5180 into tag reads: polls for a card, reads
// its whole memory (NTAG pages, MIFARE Classic sectors with vendor keys,
// Type 4 NDEF file, ISO 15693 blocks) and reports each card once until it
// is taken away.
#pragma once

#include "pn5180.h"
#include "pn532.h"
#include "rc522.h"
#include "tag.h"

enum class ReaderChip : uint8_t { PN532 = 0, RC522 = 1, PN5180 = 2 };

struct ReaderPins {
    ReaderChip chip = ReaderChip::PN532;
    Pn532Bus bus = Pn532Bus::I2C;   // PN532; the RC522 and PN5180 are always on SPI
    int sda = -1, scl = -1;     // I2C
    int rx = -1, tx = -1;       // UART (ESP32 side)
    int sck = -1, miso = -1, mosi = -1, ss = -1;    // SPI
    int rst = -1;               // RC522 / PN5180 RST, -1: not wired
    int busy = -1;              // PN5180 BUSY
};

class TagReader {
public:
    void begin(const ReaderPins &pins);
    // Call often. True when a new card was read into tag.
    bool poll(Tag &tag);
    // Read the card on the reader again on the next poll.
    void forget() { presentLen_ = 0; }
    // The chip's power-down mode (PN532, RC522), before deep sleep.
    void powerDown() { chip_->powerDown(); }

    bool connected() const { return chip_->ready(); }
    String describe() const;
    String error() const { return error_; }

private:
    bool readType2(const NfcTarget &target, Tag &tag);
    bool readMifareClassic(const NfcTarget &target, Tag &tag);
    bool readType4(const NfcTarget &target, Tag &tag);
    bool readType5(const NfcTarget &target, Tag &tag);
    bool findCard(NfcTarget &target);
    bool reselect(const NfcTarget &target);
    // The addresses that answer on the I2C bus, for the "not found" error.
    String scanI2C();
    int auth(const NfcTarget &target, int block, const uint8_t *key, bool keyB);
    int apdu(const uint8_t *cmd, size_t len, uint8_t *out, size_t expect);

    Pn532 pn532_;
    Rc522 rc522_;
    Pn5180 pn5180_;
    NfcChip *chip_ = &pn532_;
    ReaderPins pins_;
    String error_;
    uint32_t nextInit_ = 0;
    uint8_t present_[10] = {};
    uint8_t presentLen_ = 0;
    int misses_ = 0;
    // The last card was ISO 15693: look for one first.
    bool vicinityFirst_ = false;
};
