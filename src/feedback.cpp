#include "feedback.h"

namespace {
const uint16_t READ[] = {60, 0};
const uint16_t SUCCESS[] = {400, 0};
const uint16_t FAILURE[] = {80, 80, 80, 80, 80, 0};
const uint16_t WAITING[] = {60, 120, 60, 0};
const uint16_t NO_SLOT[] = {900, 0};

// What each signal shows on an RGB LED or WS2812.
constexpr uint32_t BLUE = 0x0000FF;       // read, slot count
constexpr uint32_t GREEN = 0x00FF00;      // the printer took it
constexpr uint32_t RED = 0xFF0000;        // failed
constexpr uint32_t YELLOW = 0xFFA000;     // waiting for a slot
constexpr uint32_t PURPLE = 0xFF00FF;     // setup access point

// A WS2812 at full power is blinding (and draws 60 mA).
constexpr uint8_t WS2812_SCALE = 48;

// A held spool color with its brightest channel below this is "black"
// (shown as a slow white pulse); one with less saturation than this
// (out of 255) is a grey or white (shown as steady white, dimmer for darker).
constexpr uint8_t BLACK_MAX = 0x30;
constexpr uint8_t GREY_SATURATION = 64;
constexpr uint8_t GREY_MIN_LEVEL = 40;     // the dimmest grey that still reads as lit
constexpr uint32_t PULSE_PERIOD = 2000;    // ms, black's breathing
constexpr uint8_t PULSE_LOW = 10, PULSE_HIGH = 120;

uint32_t white(uint8_t level) { return (uint32_t)level << 16 | (uint32_t)level << 8 | level; }
}  // namespace

const char *ledTypeName(LedType type) {
    switch (type) {
    case LedType::Rgb: return "rgb";
    case LedType::Ws2812: return "ws2812";
    default: return "single";
    }
}

void Feedback::begin(const LedConfig &led, int buzzerPin) {
    led_ = led;
    buzzer_ = buzzerPin;
    if (led_.type != LedType::Ws2812) {
        if (led_.pin >= 0) pinMode(led_.pin, OUTPUT);
        if (led_.type == LedType::Rgb && led_.green >= 0) pinMode(led_.green, OUTPUT);
        if (led_.type == LedType::Rgb && led_.blue >= 0) pinMode(led_.blue, OUTPUT);
    }
    if (buzzer_ >= 0) pinMode(buzzer_, OUTPUT);
    output(false);
}

void Feedback::setLed(uint32_t color) {
    if (color == shown_) return;
    shown_ = color;
    uint8_t r = color >> 16, g = color >> 8, b = color;
    auto write = [&](int pin, bool on) {
        if (pin >= 0) digitalWrite(pin, on != led_.activeLow ? HIGH : LOW);
    };
    switch (led_.type) {
    case LedType::Single: write(led_.pin, color != 0); break;
    case LedType::Rgb:
        write(led_.pin, r >= 0x40);
        write(led_.green, g >= 0x40);
        write(led_.blue, b >= 0x40);
        break;
    case LedType::Ws2812:
        if (led_.pin < 0) break;
        r = r * WS2812_SCALE / 255;
        g = g * WS2812_SCALE / 255;
        b = b * WS2812_SCALE / 255;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        rgbLedWrite(led_.pin, r, g, b);
#else
        neopixelWrite(led_.pin, r, g, b);
#endif
        break;
    }
}

void Feedback::output(bool on) {
    setLed(on ? color_ : 0);
    // An active buzzer: sounds while the pin is high.
    if (buzzer_ >= 0) digitalWrite(buzzer_, on && pattern_ ? HIGH : LOW);
}

void Feedback::play(Signal signal) {
    holdUntil_ = 0;
    holdMs_ = 0;
    switch (signal) {
    case Signal::Read: start(READ, BLUE); break;
    case Signal::Success: start(SUCCESS, GREEN); break;
    case Signal::Failure: start(FAILURE, RED); break;
    case Signal::Waiting: start(WAITING, YELLOW); break;
    }
}

void Feedback::count(int n) {
    holdUntil_ = 0;
    holdMs_ = 0;
    if (n <= 0) {
        start(NO_SLOT, BLUE);
        return;
    }
    n = min(n, 8);
    for (int i = 0; i < n; i++) {
        counted_[2 * i] = 200;
        counted_[2 * i + 1] = 250;
    }
    counted_[2 * n - 1] = 0;
    start(counted_, BLUE);
}

void Feedback::holdColor(uint32_t color, uint32_t ms) {
    if (led_.type != LedType::Ws2812 || led_.pin < 0 || !ms) return;
    hold_ = color & 0xFFFFFF;
    holdMs_ = ms;
    // Without a pattern playing, the hold starts now.
    if (!pattern_) holdUntil_ = millis() + ms;
}

uint32_t Feedback::heldLed(uint32_t now) const {
    uint8_t r = hold_ >> 16, g = hold_ >> 8, b = hold_;
    uint8_t hi = max(r, max(g, b)), lo = min(r, min(g, b));
    if (hi < BLACK_MAX) {
        // Black: white breathing slowly between dim and bright.
        uint32_t phase = now % PULSE_PERIOD, half = PULSE_PERIOD / 2;
        uint32_t ramp = phase < half ? phase : PULSE_PERIOD - phase;
        return white(PULSE_LOW + (PULSE_HIGH - PULSE_LOW) * ramp / half);
    }
    if ((uint32_t)(hi - lo) * 255 / hi < GREY_SATURATION)
        return white(max<uint8_t>(hi, GREY_MIN_LEVEL));    // grey to white
    // A color: full brightness, same hue, so dark shades still show.
    return (uint32_t)(r * 255 / hi) << 16 | (uint32_t)(g * 255 / hi) << 8 | (b * 255 / hi);
}

void Feedback::start(const uint16_t *pattern, uint32_t color) {
    pattern_ = pattern;
    color_ = color;
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
            if (holdMs_) holdUntil_ = now + holdMs_;
            holdMs_ = 0;
            return;
        }
        output(step_ % 2 == 0);       // even steps on, odd steps off
        return;
    }
    if (holdUntil_) {
        if ((int32_t)(now - holdUntil_) < 0) {
            setLed(heldLed(now));
            return;
        }
        holdUntil_ = 0;
        setLed(0);
    }
    if (idleBlink_) {
        bool on = (now / 1000) % 2 == 0 && now % 1000 < 100;
        setLed(on ? PURPLE : 0);
    }
}
