// The 2.8" 320x240 touch screen of the ESP32-2432S028 ("Cheap Yellow
// Display"): ILI9341 (or ST7789 on the two-USB version) on HSPI, XPT2046
// touch on its own pins. Besides what the OLED shows, it has the filament's
// color and buttons: the header picks the printer, the bottom row the
// fixed slot, and a scan waiting for a slot can be put into one here.
// Also the 1.47" 320x172 ST7789 of the Waveshare ESP32-C6-LCD-1.47: the
// same screens, tighter and without touch.
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
    void off();

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
    int slotWidth() const { return w_ / 5; }    // Ask, 1-4

    class CydLcd *lcd_ = nullptr;
    String shown_;
    int w_ = 320, h_ = 240;
    int header_ = 40, footer_ = 192;            // footer: top of the slot buttons
    bool compact_ = false;                      // 172 high: tighter rows
    bool touch_ = true;
    bool flip_ = false;
    bool asleep_ = false;
    bool touchDown_ = false;
    uint32_t changedAt_ = 0, lastDraw_ = 0, lastTouch_ = 0;
    int sleepMinutes_ = 0;
};

#endif
