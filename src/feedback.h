// LED and buzzer signals, played without blocking the main loop.
#pragma once

#include <Arduino.h>

enum class Signal : uint8_t { Read, Success, Failure, Waiting };

// A plain LED, an RGB LED on three pins (each color on or off), or one
// addressable WS2812 ("NeoPixel") on one pin.
enum class LedType : uint8_t { Single = 0, Rgb = 1, Ws2812 = 2 };

struct LedConfig {
    LedType type = LedType::Single;
    int pin = -1;               // the LED, red, or the WS2812's data pin
    int green = -1, blue = -1;  // RGB
    bool activeLow = false;     // Single and RGB
};

const char *ledTypeName(LedType type);

class Feedback {
public:
    void begin(const LedConfig &led, int buzzerPin);
    void play(Signal signal);
    // Blinks n times (a slot number); 0 is one long blink (no fixed slot).
    void count(int n);
    // Slow blink while there is no Wi-Fi (setup access point).
    void setIdleBlink(bool on) { idleBlink_ = on; }
    // LED and buzzer off, the pattern dropped (before deep sleep).
    void stop() {
        pattern_ = nullptr;
        idleBlink_ = false;
        output(false);
    }
    void loop();

private:
    void output(bool on);
    // 0xRRGGBB; a plain LED lights for any color but black.
    void setLed(uint32_t color);
    void start(const uint16_t *pattern, uint32_t color);

    LedConfig led_;
    int buzzer_ = -1;
    bool idleBlink_ = false;
    uint32_t shown_ = 0xFFFFFFFF;   // the LED's color, to write only changes
    // Pattern: on/off durations in ms, 0-terminated, shown in color_.
    const uint16_t *pattern_ = nullptr;
    uint32_t color_ = 0;
    uint8_t step_ = 0;
    uint32_t stepStart_ = 0;
    uint16_t counted_[2 * 8 + 1] = {};
};
