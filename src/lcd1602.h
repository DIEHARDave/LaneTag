// A 16x2 character LCD (HD44780) on a PCF8574 I2C backpack, the common
// "1602 I2C" module. The backpack wires P0 RS, P1 RW, P2 EN, P3 backlight
// and P4-P7 to D4-D7; the LCD runs in 4-bit mode and is only written to.
#pragma once

#include <Arduino.h>
#include <Wire.h>

class Lcd1602 {
public:
    static constexpr int COLS = 16, ROWS = 2;

    void begin(TwoWire *bus, uint8_t address);
    // One full row (padded or cut to 16 characters); bytes are the LCD's
    // own character codes (see toLcd()).
    void row(int row, const String &text);
    // Display and backlight on or off.
    void power(bool on);

    // UTF-8 to the LCD's ROM (A00): ASCII as it is, a degree sign as 0xDF,
    // anything else as '?'.
    static String toLcd(const String &utf8);

private:
    void command(uint8_t value);
    void send(uint8_t value, uint8_t mode);
    void nibble(uint8_t bits);

    TwoWire *bus_ = nullptr;
    uint8_t address_ = 0x27;
    uint8_t backlight_ = 0x08;
};
