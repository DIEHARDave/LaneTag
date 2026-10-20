#include "iso_dep.h"

namespace {

constexpr uint32_t ACTIVATION_TIMEOUT_MS = 10;
// RATS with FSDI 4 and CID 0: frames from the card up to 48 bytes, CRC
// included, so they always fit the RC522's 64 byte FIFO; longer answers
// come chained.
const uint8_t RATS[] = {0xE0, 0x40};
const uint16_t FSC_TABLE[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};

}  // namespace

bool IsoDep::activate() {
    active_ = false;
    uint8_t ats[64];
    int n = link_.transceiveFrame(RATS, sizeof(RATS), ats, sizeof(ats), ACTIVATION_TIMEOUT_MS);
    if (n < 1) return false;
    int fsci = 2, fwi = 4, sfgi = 0;
    if (n >= 2) {
        uint8_t t0 = ats[1];
        fsci = t0 & 0x0F;
        int pos = 2;
        if (t0 & 0x10) pos++;               // TA(1): bit rates, 106 kbps kept
        if ((t0 & 0x20) && pos < n) {       // TB(1)
            fwi = ats[pos] >> 4;
            sfgi = ats[pos] & 0x0F;
        }
    }
    fsc_ = fsci < 9 ? FSC_TABLE[fsci] : 256;
    if (fwi > 14) fwi = 4;
    // FWT = 256 * 16 / fc * 2^FWI, about 302 us * 2^FWI.
    fwtMs_ = ((302UL << fwi) + 999) / 1000 + 5;
    if (sfgi && sfgi < 15) delayMicroseconds(min(302UL << sfgi, 1000000UL));
    blockNumber_ = 0;
    active_ = true;
    return true;
}

int IsoDep::exchange(const uint8_t *apdu, size_t len, uint8_t *out, size_t expect) {
    if (!active_) return -1;
    uint8_t frame[64];
    if (len + 3 > min(fsc_, sizeof(frame))) return -1;
    frame[0] = 0x02 | blockNumber_;
    memcpy(frame + 1, apdu, len);
    size_t frameLen = len + 1;
    uint32_t wait = fwtMs_;
    size_t total = 0;
    while (true) {
        uint8_t resp[64];
        int n = link_.transceiveFrame(frame, frameLen, resp, sizeof(resp), wait);
        if (n < 1) return -1;
        uint8_t pcb = resp[0];
        if ((pcb & 0xF7) == 0xF2 && n >= 2) {     // S(WTX): the card needs longer
            uint8_t wtxm = resp[1] & 0x3F;
            frame[0] = pcb;
            frame[1] = wtxm;
            frameLen = 2;
            wait = fwtMs_ * max<uint8_t>(wtxm, 1);
            continue;
        }
        if ((pcb & 0xC0) != 0x00) return -1;      // not an I-block
        blockNumber_ ^= 1;
        int header = 1 + ((pcb & 0x08) ? 1 : 0) + ((pcb & 0x04) ? 1 : 0);
        for (int i = header; i < n; i++)
            if (total < expect) out[total++] = resp[i];
        if (!(pcb & 0x10)) return total;          // last block of the answer
        frame[0] = 0xA2 | blockNumber_;           // R(ACK): send the next one
        frameLen = 1;
        wait = fwtMs_;
    }
}

void IsoDep::deselect() {
    if (!active_) return;
    const uint8_t deselect[] = {0xC2};
    uint8_t resp[4];
    link_.transceiveFrame(deselect, sizeof(deselect), resp, sizeof(resp), fwtMs_);
    active_ = false;
}
