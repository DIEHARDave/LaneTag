// LED and buzzer signals, played without blocking the main loop.
#pragma once

#include <Arduino.h>

enum class Signal : uint8_t { Read, Success, Failure, Waiting };

class Feedback {
public:
    void begin(int ledPin, bool ledActiveLow, int buzzerPin);
    void play(Signal signal);
    // Blinks n times (a slot number); 0 is one long blink (no fixed slot).
    void count(int n);
    // Slow blink while there is no Wi-Fi (setup access point).
    void setIdleBlink(bool on) { idleBlink_ = on; }
    void loop();

private:
    void output(bool on);
    void start(const uint16_t *pattern);

    int led_ = -1, buzzer_ = -1;
    bool ledLow_ = false;
    bool idleBlink_ = false;
    // Pattern: on/off durations in ms, 0-terminated.
    const uint16_t *pattern_ = nullptr;
    uint8_t step_ = 0;
    uint32_t stepStart_ = 0;
    uint16_t counted_[2 * 8 + 1] = {};
};
