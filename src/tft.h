// The 2.8" 320x240 touch screen of the ESP32-2432S028 ("Cheap Yellow
// Display"): ILI9341 (or ST7789 on the two-USB version) on HSPI, XPT2046
// touch on its own pins. Besides what the OLED shows, it has the filament's
// color and buttons: the header picks the printer, the bottom row the
// fixed slot, and a scan waiting for a slot can be put into one here.
#pragma once

#ifdef HAS_TFT

#include <Arduino.h>

#include "display.h"

class Tft {
public:
    void begin(const DisplayConfig &config);
    void loop();
    void refresh() { draw(true); }
    void showSlot() { wake(); draw(true); }
    void wake();

private:
    enum class Screen : uint8_t { Setup, NoReader, Idle, Scan };

    void draw(bool force);
    // What is on the screen, as text: redraw only when it changes.
    String contents(Screen screen) const;
    Screen current() const;
    void drawHeader();
    void drawSlots();
    void drawSetup();
    void drawNoReader();
    void drawIdle();
    void drawScan();
    // Word-wrapped text in the current font; returns the y below it.
    int wrap(const String &text, int x, int y, int width, int maxLines);
    void touched(int x, int y);
    bool readTouch(int &x, int &y);

    class CydLcd *lcd_ = nullptr;
    String shown_;
    bool flip_ = false;
    bool asleep_ = false;
    bool touchDown_ = false;
    uint32_t changedAt_ = 0, lastDraw_ = 0, lastTouch_ = 0;
    int sleepMinutes_ = 0;
};

#endif
