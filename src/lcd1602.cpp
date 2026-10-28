#include "lcd1602.h"

namespace {

constexpr uint8_t RS = 0x01, EN = 0x04, BACKLIGHT = 0x08;

}  // namespace

void Lcd1602::begin(TwoWire *bus, uint8_t address) {
    bus_ = bus;
    address_ = address;
    backlight_ = BACKLIGHT;
    // Init by instruction (three times 8-bit, then 4-bit): works whatever
    // state the LCD came up in, also after a reset of the ESP32 alone.
    delay(50);
    nibble(0x30);
    delayMicroseconds(4500);
    nibble(0x30);
    delayMicroseconds(4500);
    nibble(0x30);
    delayMicroseconds(150);
    nibble(0x20);
    command(0x28);  // 4-bit, two lines, 5x8
    command(0x0C);  // display on, no cursor
    command(0x01);  // clear
    delay(2);
    command(0x06);  // left to right, no shift
}

void Lcd1602::row(int row, const String &text) {
    if (!bus_) return;
    command(0x80 | (row ? 0x40 : 0x00));
    for (int i = 0; i < COLS; i++) send(i < (int)text.length() ? (uint8_t)text[i] : ' ', RS);
}

void Lcd1602::power(bool on) {
    if (!bus_) return;
    backlight_ = on ? BACKLIGHT : 0;
    command(on ? 0x0C : 0x08);
}

void Lcd1602::command(uint8_t value) { send(value, 0); }

void Lcd1602::send(uint8_t value, uint8_t mode) {
    nibble((value & 0xF0) | mode);
    nibble((value << 4) | mode);
}

// One nibble with an EN pulse: RS and data first, then EN up and down.
// The PCF8574 is a 100 kHz part: each byte takes ~90 us there, longer
// than the setup times, the pulse and most commands need.
void Lcd1602::nibble(uint8_t bits) {
    bits |= backlight_;
    bus_->beginTransmission(address_);
    bus_->write(bits);
    bus_->write(bits | EN);
    bus_->write(bits);
    if (bus_->endTransmission() != 0) errors_++;
    delayMicroseconds(50);
}

String Lcd1602::toLcd(const String &utf8) {
    String out;
    out.reserve(utf8.length());
    for (int i = 0; i < (int)utf8.length(); i++) {
        uint8_t c = utf8[i];
        if (c < 0x80) {
            out += (char)c;
            continue;
        }
        if (c == 0xC2 && i + 1 < (int)utf8.length() && (uint8_t)utf8[i + 1] == 0xB0)
            out += (char)0xDF;
        else if (c >= 0xC0)
            out += '?';
        // Continuation bytes (0x80-0xBF) are part of the character above.
        if (c >= 0xC0)
            while (i + 1 < (int)utf8.length() && ((uint8_t)utf8[i + 1] & 0xC0) == 0x80) i++;
    }
    return out;
}
