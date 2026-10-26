// NXP MFRC522 ("RC522" modules) over SPI. ISO 14443A only, like the PN532:
// NTAG / Ultralight, MIFARE Classic (the chip does Crypto1 itself) and
// ISO-DEP (Type 4), whose block protocol IsoDep adds because the RC522,
// unlike the PN532, has none.
//
// The common blue RC522 boards are wired for SPI; power them from 3.3 V.
#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "iso_dep.h"
#include "nfc_chip.h"

class Rc522 : public NfcChip, private FrameLink {
public:
    Rc522() : isoDep_(*this) {}
    // rst: the RST (NRSTPD) pin, or -1 when it is tied to 3.3 V.
    void attach(SPIClass &spi, int sck, int miso, int mosi, int ss, int rst);
    bool init() override;
    void end() override;
    bool ready() const override { return ok_; }
    const char *chipName() const override { return "RC522"; }
    const char *busName() const override { return "SPI"; }
    String firmware() const override { return firmware_; }

    bool listTarget(NfcTarget &target) override;
    int exchange(const uint8_t *data, size_t len, uint8_t *out, size_t expect,
                 uint32_t timeoutMs = 1000) override;
    void release() override;
    void powerDown() override;
    int failures() const override { return failures_; }

private:
    uint8_t readReg(uint8_t reg);
    void readFifo(uint8_t *out, size_t len);
    void writeReg(uint8_t reg, uint8_t value);
    void writeReg(uint8_t reg, const uint8_t *data, size_t len);
    void setBits(uint8_t reg, uint8_t mask) { writeReg(reg, readReg(reg) | mask); }
    void clearBits(uint8_t reg, uint8_t mask) { writeReg(reg, readReg(reg) & ~mask); }
    void setTimeout(uint32_t ms);
    bool chipAnswers();

    // Run Transceive or MFAuthent. Returns the bytes received, or -1 on a
    // timeout, a collision or a framing error. txBits: bits of the last
    // byte sent (0: all 8); rxBits receives the same for the answer.
    int transceive(uint8_t command, const uint8_t *data, size_t len, uint8_t *out, size_t max,
                   uint8_t txBits = 0, uint8_t *rxBits = nullptr);
    // A frame with CRC_A added, an answer with CRC_A checked and removed.
    int transceiveCrc(const uint8_t *data, size_t len, uint8_t *out, size_t max);
    int transceiveFrame(const uint8_t *data, size_t len, uint8_t *out, size_t max,
                        uint32_t timeoutMs) override;
    bool select(NfcTarget &target);

    SPIClass *spi_ = nullptr;
    int ss_ = -1, rst_ = -1;
    bool ok_ = false;
    int failures_ = 0;
    String firmware_;
    uint8_t version_ = 0;       // VersionReg, to notice a chip that went away
    IsoDep isoDep_;
};
