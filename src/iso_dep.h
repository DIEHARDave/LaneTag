// ISO 14443-4 (ISO-DEP) block protocol for chips that only send and
// receive frames (RC522, PN5180); the PN532 does this itself.
#pragma once

#include <Arduino.h>

class FrameLink {
public:
    virtual ~FrameLink() = default;
    // Send a frame with CRC_A added; the card's answer with its CRC checked
    // and removed, or -1.
    virtual int transceiveFrame(const uint8_t *data, size_t len, uint8_t *out, size_t max,
                                uint32_t timeoutMs) = 0;
};

class IsoDep {
public:
    explicit IsoDep(FrameLink &link) : link_(link) {}
    // RATS to a selected card.
    bool activate();
    // I-blocks without CID or NAD. The APDUs TagReader sends fit in one
    // frame; answers longer than one frame are chained and acknowledged.
    int exchange(const uint8_t *apdu, size_t len, uint8_t *out, size_t expect);
    // S(DESELECT): the card halts, a WUPA wakes it.
    void deselect();
    bool active() const { return active_; }
    void reset() { active_ = false; }

private:
    FrameLink &link_;
    bool active_ = false;
    uint8_t blockNumber_ = 0;
    size_t fsc_ = 32;           // the card's frame size, with PCB and CRC
    uint32_t fwtMs_ = 5;        // the card's frame waiting time
};
