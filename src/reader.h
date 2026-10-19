// Turns a PN532 into tag reads: polls for a card, reads its whole memory
// (NTAG pages, MIFARE Classic sectors with vendor keys, Type 4 NDEF file)
// and reports each card once until it is taken away.
#pragma once

#include "pn532.h"
#include "tag.h"

struct ReaderPins {
    Pn532Bus bus = Pn532Bus::I2C;
    int sda = -1, scl = -1;     // I2C
    int rx = -1, tx = -1;       // UART (ESP32 side)
};

class TagReader {
public:
    void begin(const ReaderPins &pins);
    // Call often. True when a new card was read into tag.
    bool poll(Tag &tag);
    // Read the card on the reader again on the next poll.
    void forget() { presentLen_ = 0; }

    bool connected() const { return chip_.ready(); }
    String describe() const;
    String error() const { return error_; }

private:
    bool readType2(const Pn532Target &target, Tag &tag);
    bool readMifareClassic(const Pn532Target &target, Tag &tag);
    bool readType4(const Pn532Target &target, Tag &tag);
    bool reselect(const Pn532Target &target);
    int auth(const Pn532Target &target, int block, const uint8_t *key, bool keyB);
    int apdu(const uint8_t *cmd, size_t len, uint8_t *out, size_t expect);

    Pn532 chip_;
    ReaderPins pins_;
    String error_;
    uint32_t nextInit_ = 0;
    uint8_t present_[10] = {};
    uint8_t presentLen_ = 0;
    int misses_ = 0;
};
