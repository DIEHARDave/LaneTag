// An optional screen: a 128x64 I2C OLED (SSD1306 or SH1106), the 2.8"
// touch screen of the ESP32-2432S028 or the 1.47" one of the Waveshare
// ESP32-C6-LCD-1.47 (see tft.h). It shows the active
// printer and its slot, the filament of the last scan and what the printer
// made of it.
#pragma once

#include <Arduino.h>

#include "reader.h"

enum class DisplayType : uint8_t { None = 0, SSD1306 = 1, SH1106 = 2, CydIli9341 = 3, CydSt7789 = 4, C6Lcd147 = 5 };

inline bool isTft(DisplayType type) {
    return type == DisplayType::CydIli9341 || type == DisplayType::CydSt7789 || type == DisplayType::C6Lcd147;
}

struct DisplayConfig {
    DisplayType type = DisplayType::None;
    int sda = -1, scl = -1;     // OLED
    uint8_t address = 0x3C;     // 7-bit; 0x3D on some modules
    bool flip = false;          // turned 180 degrees
    int sleepMinutes = 10;      // screen off after this long without a change; 0: never
};

const char *displayTypeName(DisplayType type);
// A color name for the screen: the nearest of a few common filament
// colors.
const char *colorName(uint32_t color);

class Display {
public:
    // After the reader's begin(), so a display on the reader's I2C pins
    // shares its bus.
    void begin(const DisplayConfig &config, const ReaderPins &reader);
    // Redraws when something changed; cheap to call every loop.
    void loop();
    // Draw now (before a call that blocks the loop, such as a scan going
    // to the printer).
    void refresh();
    // Show the active printer's slot in large type for a few seconds.
    void showSlot();
    // Wake the screen.
    void wake();
    // Screen off (before deep sleep).
    void off();

    bool enabled() const { return u8g2_ != nullptr || tft_ != nullptr; }
    String error() const { return error_; }

private:
    void draw(bool force);
    void render();
    void renderSlot();
    void renderSetup();
    void renderScan();
    void renderIdle();
    void footer();
    void title(const String &text);
    // Word-wrapped text in the small font; returns the y after the last line.
    int wrap(const String &text, int y, int maxLines);

    class U8G2 *u8g2_ = nullptr;
    class Tft *tft_ = nullptr;
    String error_;
    uint8_t shown_[1024];
    bool asleep_ = false;
    uint32_t changedAt_ = 0, lastDraw_ = 0, slotUntil_ = 0;
    int sleepMinutes_ = 0;
};
