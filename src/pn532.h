// NXP PN532 over I2C, SPI or HSU (UART). ISO 14443A only: NTAG /
// Ultralight, MIFARE Classic and ISO-DEP (Type 4). Port of FF_NFC's
// pn532.py.
//
// Board switches: I2C is SEL0=1 SEL1=0 (red/blue "PN532 NFC V3" boards:
// switch 1 ON, 2 OFF); SPI is SEL0=0 SEL1=1 (switch 1 OFF, 2 ON); HSU is
// both OFF.
#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <Wire.h>

#include "nfc_chip.h"

enum class Pn532Bus : uint8_t { I2C = 0, UART = 1, SPI = 2 };

class Pn532 : public NfcChip {
public:
    void attachI2C(TwoWire &wire, int sda, int scl);
    void attachUart(HardwareSerial &serial, int rx, int tx, uint32_t baud = 115200);
    // rst: the PN532's RSTPDN, -1 when not wired.
    void attachSpi(SPIClass &spi, int sck, int miso, int mosi, int ss, int rst);
    // Wake the chip, read its firmware version and set short RF retries.
    bool init() override;
    // I2C: "SDA" or "SCL" when that line stays low with the bus idle (after
    // clocking out a device stuck mid-byte), else nullptr. Transfers on
    // such a bus each hang until they time out.
    const char *stuckLine();
    void end() override;
    bool ready() const override { return ok_; }
    const char *chipName() const override { return "PN532"; }
    String firmware() const override { return firmware_; }
    const char *busName() const override {
        return bus_ == Pn532Bus::I2C ? "I2C" : bus_ == Pn532Bus::SPI ? "SPI" : "UART";
    }

    // Response data after the response code, or -1. expect is the largest
    // response wanted (I2C and SPI read that many bytes in one transfer).
    int command(uint8_t cmd, const uint8_t *params, size_t len, uint8_t *out, size_t expect,
                uint32_t timeoutMs = 1000, bool wake = false);

    bool listTarget(NfcTarget &target) override;
    // InDataExchange with the selected target: the card's answer (without
    // the status byte), or -1 when the PN532 or the card reported an error.
    int exchange(const uint8_t *data, size_t len, uint8_t *out, size_t expect,
                 uint32_t timeoutMs = 1000) override;
    void release() override;
    void powerDown() override;
    int failures() const override { return failures_; }

private:
    bool writeFrame(const uint8_t *data, size_t len, bool wake);
    // 0: ACK, >0: body length (TFI onwards), -1: timeout, -2: bad frame.
    int readFrame(uint8_t *body, size_t max, uint32_t timeoutMs, size_t i2cLen);  // i2cLen: I2C and SPI
    bool i2cFill(size_t len, uint32_t deadline);
    void uartFill(uint32_t deadline);
    bool spiFill(size_t len, uint32_t deadline);
    void spiSelect();
    void spiDeselect();
    int parse(uint8_t *body, size_t max);

    Pn532Bus bus_ = Pn532Bus::I2C;
    TwoWire *wire_ = nullptr;
    int sda_ = -1, scl_ = -1;
    HardwareSerial *serial_ = nullptr;
    SPIClass *spi_ = nullptr;
    int ss_ = -1;
    bool ok_ = false;
    int failures_ = 0;
    String firmware_;
    uint8_t rx_[300];
    size_t rxLen_ = 0;
};
