#include "feedback.h"

namespace {
const uint16_t READ[] = {60, 0};
const uint16_t SUCCESS[] = {400, 0};
const uint16_t FAILURE[] = {80, 80, 80, 80, 80, 0};
const uint16_t WAITING[] = {60, 120, 60, 0};
const uint16_t NO_SLOT[] = {900, 0};
}  // namespace

void Feedback::begin(int ledPin, bool ledActiveLow, int buzzerPin) {
    led_ = ledPin;
    ledLow_ = ledActiveLow;
    buzzer_ = buzzerPin;
    if (led_ >= 0) pinMode(led_, OUTPUT);
    if (buzzer_ >= 0) pinMode(buzzer_, OUTPUT);
    output(false);
}

void Feedback::output(bool on) {
    if (led_ >= 0) digitalWrite(led_, on != ledLow_ ? HIGH : LOW);
    // An active buzzer: sounds while the pin is high.
    if (buzzer_ >= 0) digitalWrite(buzzer_, on && pattern_ ? HIGH : LOW);
}

void Feedback::play(Signal signal) {
    switch (signal) {
    case Signal::Read: start(READ); break;
    case Signal::Success: start(SUCCESS); break;
    case Signal::Failure: start(FAILURE); break;
    case Signal::Waiting: start(WAITING); break;
    }
}

void Feedback::count(int n) {
    if (n <= 0) {
        start(NO_SLOT);
        return;
    }
    n = min(n, 8);
    for (int i = 0; i < n; i++) {
        counted_[2 * i] = 200;
        counted_[2 * i + 1] = 250;
    }
    counted_[2 * n - 1] = 0;
    start(counted_);
}

void Feedback::start(const uint16_t *pattern) {
    pattern_ = pattern;
    step_ = 0;
    stepStart_ = millis();
    output(true);
}

void Feedback::loop() {
    uint32_t now = millis();
    if (pattern_) {
        if (now - stepStart_ < pattern_[step_]) return;
        step_++;
        stepStart_ = now;
        if (!pattern_[step_]) {
            pattern_ = nullptr;
            output(false);
            return;
        }
        output(step_ % 2 == 0);       // even steps on, odd steps off
        return;
    }
    if (idleBlink_ && led_ >= 0) {
        bool on = (now / 1000) % 2 == 0 && now % 1000 < 100;
        digitalWrite(led_, on != ledLow_ ? HIGH : LOW);
    }
}
