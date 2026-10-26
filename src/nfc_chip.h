// What TagReader needs from a reader chip: find one ISO 14443A card (or,
// on the PN5180, an ISO 15693 one) and exchange frames with it. Pn532,
// Rc522 and Pn5180 implement it.
#pragma once

#include <Arduino.h>

struct NfcTarget {
    uint8_t uid[10] = {};
    uint8_t uidLen = 0;
    uint8_t atqa[2] = {};
    uint8_t sak = 0;
    // ISO 15693 (NFC-V): uid is the 8 byte UID, most significant byte (E0)
    // first, as zmod_nfc reports it; atqa and sak stay zero.
    bool iso15693 = false;
    bool sameCard(const NfcTarget &other) const {
        return uidLen == other.uidLen && memcmp(uid, other.uid, uidLen) == 0;
    }
};

class NfcChip {
public:
    virtual ~NfcChip() = default;
    // Reset the chip and check that it answers.
    virtual bool init() = 0;
    virtual void end() = 0;
    virtual bool ready() const = 0;
    virtual const char *chipName() const = 0;
    virtual const char *busName() const = 0;
    virtual String firmware() const = 0;

    // Select one card (a halted one too).
    virtual bool listTarget(NfcTarget &target) = 0;
    // A command to the selected card, as the PN532's InDataExchange takes
    // it: MIFARE commands without CRC, MIFARE authentication as
    // {0x60|0x61, block, key[6], uid[4]}, and for ISO-DEP cards the APDU
    // alone. The card's answer, or -1 on any error.
    virtual int exchange(const uint8_t *data, size_t len, uint8_t *out, size_t expect,
                         uint32_t timeoutMs = 1000) = 0;
    virtual void release() = 0;
    // Failed commands in a row; the caller re-inits the chip after a few.
    virtual int failures() const = 0;
    // Into the chip's power-down mode (before deep sleep); init() wakes it.
    virtual void powerDown() {}

    // ISO 15693, on chips that have it.
    virtual bool hasIso15693() const { return false; }
    // Inventory with one slot: the card in the field, or false.
    virtual bool inventory15693(NfcTarget &) { return false; }
    // A request to the card found last; the answer after the flags byte, or
    // -1 when the card reported an error or did not answer.
    virtual int exchange15693(const uint8_t *, size_t, uint8_t *, size_t) { return -1; }
};
