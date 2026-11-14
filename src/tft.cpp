#ifdef HAS_TFT

#include "tft.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sleep.h>

#include "app.h"
#include "splash_logo.h"
#include "version.h"

// The CYD's screen: HSPI, backlight on GPIO 21. The Waveshare
// ESP32-C6-LCD-1.47's: a 172x320 ST7789 (in the middle of its 240 columns)
// on the C6's one SPI bus, backlight on GPIO 22; the ESP32-C5-LCD-1.47 has
// the same panel on other pins. The ideaspark ESP32 1.9" LCD: a 170x320
// ST7789 (also in the middle of its 240 columns) on the VSPI pins, backlight
// on GPIO 32. The ideaspark ESP32 1.14" LCD: the same pins, with a 135x240
// ST7789 in the middle of its 240x320 memory.
// The Waveshare ESP32-C6-Touch-LCD-1.47 has a JD9853 (same size and offset)
// on other pins, and an AXS5106L touch controller on I2C.
#if CONFIG_IDF_TARGET_ESP32C5
constexpr int LCD147_SCLK = 7, LCD147_MOSI = 6, LCD147_DC = 24, LCD147_CS = 23, LCD147_RST = 26, LCD147_BL = 10;
#elif defined(BOARD_LCD169)
// The Waveshare ESP32-C6-LCD-1.69: a 240x280 ST7789 (rows 20-299 of its
// 320), handled like the 1.47" ones.
constexpr int LCD147_SCLK = 1, LCD147_MOSI = 2, LCD147_DC = 3, LCD147_CS = 5, LCD147_RST = 4, LCD147_BL = 6;
#elif defined(BOARD_C6_TOUCH147)
constexpr int LCD147_SCLK = 1, LCD147_MOSI = 2, LCD147_DC = 15, LCD147_CS = 14, LCD147_RST = 22, LCD147_BL = 23;
#else
constexpr int LCD147_SCLK = 7, LCD147_MOSI = 6, LCD147_DC = 15, LCD147_CS = 14, LCD147_RST = 21, LCD147_BL = 22;
#endif

#ifdef BOARD_C6_TOUCH147
// The JD9853: an ST7789-like command set, with its own power and gamma
// setup (from Waveshare's demo for the board).
struct Panel_JD9853 : public lgfx::Panel_LCD {
    Panel_JD9853() {
        _cfg.panel_height = _cfg.memory_height = 320;
        _cfg.dummy_read_pixel = 16;
    }

protected:
    const uint8_t *getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            CMD_SLPOUT, CMD_INIT_DELAY, 120,
            0xDF, 2, 0x98, 0x53,
            0xB2, 1, 0x23,
            0xB7, 4, 0x00, 0x47, 0x00, 0x6F,
            0xBB, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
            0xC0, 2, 0x44, 0xA4,
            0xC1, 1, 0x16,
            0xC3, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
            0xC4, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
            0xC8, 32, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
                      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
            0xD0, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
            0xD7, 2, 0x00, 0x30,
            0xE6, 1, 0x14,
            0xDE, 1, 0x01,
            0xB7, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
            0xC1, 3, 0x14, 0x15, 0xC0,
            0xC2, 2, 0x06, 0x3A,
            0xC4, 2, 0x72, 0x12,
            0xBE, 1, 0x00,
            0xDE, 1, 0x02,
            0xE5, 3, 0x00, 0x02, 0x00,
            0xE5, 3, 0x01, 0x02, 0x00,
            0xDE, 1, 0x00,
            0x35, 1, 0x00,
            0xDE, 1, 0x02,
            0xE5, 3, 0x00, 0x02, 0x00,
            0xDE, 1, 0x00,
            CMD_DISPON, CMD_INIT_DELAY, 10,
            0xFF, 0xFF,
        };
        return listno == 0 ? list0 : nullptr;
    }
};
#endif

class CydLcd : public lgfx::LGFX_Device {
public:
    explicit CydLcd(DisplayType type) {
        bool lcd169 = type == DisplayType::Lcd169;
        bool c6 = type == DisplayType::C6Lcd147 || lcd169;
        bool lcd114 = type == DisplayType::Lcd114;
        bool lcd19 = type == DisplayType::Lcd19 || lcd114;
#ifdef BOARD_C6_TOUCH147
        lgfx::Panel_Device *panel = &jd9853_;
#else
        lgfx::Panel_Device *panel = type == DisplayType::CydIli9341 ? (lgfx::Panel_Device *)&ili9341_ : &st7789_;
#endif
        {
            auto c = bus_.config();
#if CONFIG_IDF_TARGET_ESP32
            c.spi_host = lcd19 ? VSPI_HOST : HSPI_HOST;
#else
            c.spi_host = SPI2_HOST;
#endif
            c.spi_mode = 0;
            c.freq_write = 40000000;
            c.freq_read = 16000000;
            c.spi_3wire = false;
            c.use_lock = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = lcd19 ? 18 : c6 ? LCD147_SCLK : 14;
            c.pin_mosi = lcd19 ? 23 : c6 ? LCD147_MOSI : 13;
            c.pin_miso = lcd19 || c6 ? -1 : 12;
            c.pin_dc = c6 ? LCD147_DC : 2;
            bus_.config(c);
            panel->setBus(&bus_);
        }
        {
            auto c = panel->config();
            c.pin_cs = c6 ? LCD147_CS : 15;
            c.pin_rst = lcd19 ? 4 : c6 ? LCD147_RST : -1;
            c.pin_busy = -1;
            c.panel_width = lcd114 ? 135 : lcd19 ? 170 : lcd169 ? 240 : c6 ? 172 : 240;
            c.panel_height = lcd114 ? 240 : lcd169 ? 280 : 320;
            c.memory_width = 240;
            c.memory_height = 320;
            c.offset_x = lcd114 ? 52 : lcd19 ? 35 : lcd169 ? 0 : c6 ? 34 : 0;
            c.offset_y = lcd114 ? 40 : lcd169 ? 20 : 0;
            c.readable = !c6 && !lcd19;
            c.invert = c6 || lcd19;
#ifdef BOARD_C6_TOUCH147
            c.rgb_order = true;     // the JD9853 reads the order bit the other way round
#else
            c.rgb_order = false;
#endif
            c.bus_shared = false;
            panel->config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = lcd19 ? 32 : c6 ? LCD147_BL : 21;
            c.invert = false;
            c.freq = 12000;
            c.pwm_channel = c6 ? 0 : 7;     // the C6 and C5 have six channels
            light_.config(c);
            panel->setLight(&light_);
        }
        setPanel(panel);
    }

private:
    lgfx::Panel_ILI9341 ili9341_;
    lgfx::Panel_ST7789 st7789_;
#ifdef BOARD_C6_TOUCH147
    Panel_JD9853 jd9853_;
#endif
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

namespace {

#ifdef BOARD_C6_TOUCH147
// The AXS5106L touch controller of the ESP32-C6-Touch-LCD-1.47, on I2C
// (shared with a PN532 on I2C, which then uses these pins too). It reports
// portrait 172x320 points.
constexpr int AXS_SDA = 18, AXS_SCL = 19, AXS_RST = 20;
constexpr uint8_t AXS_ADDR = 0x63, AXS_TOUCH_REG = 0x01;
constexpr uint32_t AXS_POLL = 20;

// Polled, a full 14-byte report each time (its INT line did not fire on
// this board). It NAKs a repeated start, so the register is written with a
// stop before the read.
bool axsPressed = false;
int axsX = 0, axsY = 0;
uint32_t axsReadAt = 0;

void axsBegin() {
    Wire.begin(AXS_SDA, AXS_SCL);
    pinMode(AXS_RST, OUTPUT);
    digitalWrite(AXS_RST, LOW);
    delay(20);
    digitalWrite(AXS_RST, HIGH);
    delay(100);
}

// Portrait points while pressed.
bool axsRead(int &x, int &y) {
    if (millis() - axsReadAt >= AXS_POLL) {
        axsReadAt = millis();
        uint8_t data[14] = {0};
        Wire.beginTransmission(AXS_ADDR);
        Wire.write(AXS_TOUCH_REG);
        bool ok = Wire.endTransmission() == 0 && Wire.requestFrom(AXS_ADDR, (uint8_t)14) == 14;
        if (ok) Wire.readBytes(data, 14);
        uint8_t points = data[1] & 0x0F;
        axsPressed = ok && points > 0 && points <= 5;
        if (axsPressed) {
            axsX = ((data[2] & 0x0F) << 8) | data[3];
            axsY = ((data[4] & 0x0F) << 8) | data[5];
#ifdef TOUCH_DEBUG
            Serial.printf("axs %d %d\n", axsX, axsY);
#endif
        }
    }
    x = axsX;
    y = axsY;
    return axsPressed;
}
#endif

#ifndef BOARD_C6_TOUCH147
// The XPT2046 touch controller, bit-banged on its own pins (LovyanGFX's
// driver read a fixed Y on a CYD).
constexpr int T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33;
constexpr int Z_THRESHOLD = 400;

void touchBegin() {
    pinMode(T_CLK, OUTPUT);
    pinMode(T_MOSI, OUTPUT);
    pinMode(T_CS, OUTPUT);
    pinMode(T_MISO, INPUT);
    digitalWrite(T_CS, HIGH);
    digitalWrite(T_CLK, LOW);
}

// One conversion: the command byte, then 16 clocks for the 12-bit result.
uint16_t touchRead(uint8_t command) {
    for (int bit = 7; bit >= 0; bit--) {
        digitalWrite(T_MOSI, (command >> bit) & 1);
        digitalWrite(T_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(T_CLK, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(T_MOSI, LOW);
    uint16_t value = 0;
    for (int i = 0; i < 16; i++) {
        digitalWrite(T_CLK, HIGH);
        delayMicroseconds(1);
        value = (value << 1) | digitalRead(T_MISO);
        digitalWrite(T_CLK, LOW);
        delayMicroseconds(1);
    }
    return value >> 3;
}

// Raw 0-4095 readings while pressed.
bool touchRaw(int &x, int &y) {
    digitalWrite(T_CS, LOW);
    int z = touchRead(0xB1) + 4095 - touchRead(0xC1);
    bool pressed = z >= Z_THRESHOLD;
    if (pressed) {
        touchRead(0xD1);        // the first reading after a switch is noisy
        x = (touchRead(0xD1) + touchRead(0xD1) + touchRead(0xD1)) / 3;
        touchRead(0x91);
        y = (touchRead(0x91) + touchRead(0x91) + touchRead(0x91)) / 3;
    }
    touchRead(0xD0);            // power down, pen interrupt on
    digitalWrite(T_CS, HIGH);
    return pressed;
}
#endif

}  // namespace

namespace {

constexpr int PICK_X = 8;
constexpr uint32_t SCAN_SHOWN = 5 * 60 * 1000;
constexpr uint32_t DRAW_INTERVAL = 200;
constexpr uint32_t TOUCH_REPEAT = 250;
constexpr uint8_t BRIGHTNESS = 200;
constexpr uint32_t SPLASH_SHOWN = 3000;
// A filament name too long for its line scrolls: still for a moment, then
// one pixel per step, round with a gap.
constexpr uint32_t SCROLL_PAUSE = 2000, SCROLL_STEP = 40;
constexpr int SCROLL_GAP = 40;

// Raw XPT2046 readings at the screen's edges (landscape, rotation 1): X from
// the controller's Y channel, Y from its X channel.
constexpr int TOUCH_X0 = 190, TOUCH_X1 = 3820, TOUCH_Y0 = 390, TOUCH_Y1 = 3750;

constexpr uint32_t BG = 0x101418u, PANEL = 0x1F2933u, LINE = 0x3A4652u, TEXT = 0xFFFFFFu;
constexpr uint32_t MUTED = 0x9AA5B1u, ACCENT = 0x2F80EDu, GOOD = 0x3CCB6Au, BAD = 0xF0504Au, WARN = 0xF2A33Au;

const lgfx::IFont *const BIG = &fonts::FreeSansBold18pt7b;
const lgfx::IFont *const TITLE = &fonts::FreeSansBold12pt7b;
const lgfx::IFont *const BODY = &fonts::FreeSans9pt7b;
const lgfx::IFont *const BODY_BOLD = &fonts::FreeSansBold9pt7b;
const lgfx::IFont *const SMALL = &fonts::Font2;

// The first slot button: Ask (0) where the printer's screen asks, else 1.
int slotRowFirst(const PrinterConfig &printer) { return printer.screenAsk() ? 0 : 1; }

// The filament's name in its color, or light grey when that is too dark to
// read on the dark background (or unknown).
uint32_t spoolTextColor(int color) {
    if (color < 0) return TEXT;
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 < 80 ? 0xC8CDD2u : (uint32_t)color;
}

String fit(lgfx::LGFX_Device &lcd, String text, int width) {
    if (lcd.textWidth(text.c_str()) <= width) return text;
    while (text.length() > 1 && lcd.textWidth((text + "...").c_str()) > width) text.remove(text.length() - 1);
    return text + "...";
}

String address() {
    if (app.apMode) return "setup";
    return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("no Wi-Fi");
}

bool scanShown() {
    const ScanRecord &last = app.last;
    return last.id && (last.waiting || millis() - last.at < SCAN_SHOWN);
}

}  // namespace

void Tft::begin(const DisplayConfig &config) {
    lcd_ = new CydLcd(config.type);
    lcd_->init();
#ifdef BOARD_C6_TOUCH147
    touch_ = true;
    axsBegin();
#else
    touch_ = config.type == DisplayType::CydIli9341 || config.type == DisplayType::CydSt7789;
    if (touch_) touchBegin();
#endif
    flip_ = config.flip;
    lcd_->setRotation(flip_ ? 3 : 1);
    // The 1.69" panel's corners are rounded: the edge rows keep clear of them.
    corner_ = config.type == DisplayType::Lcd169 ? 14 : 0;
    w_ = lcd_->width();
    h_ = lcd_->height();
    compact_ = h_ < 240;
    tiny_ = h_ < 160;
    header_ = tiny_ ? 24 : compact_ ? 32 : 40;
    footer_ = h_ - (tiny_ ? 28 : compact_ ? 34 : 48);
    lcd_->setBrightness(BRIGHTNESS);
    lcd_->setTextDatum(textdatum_t::top_left);
    sleepMinutes_ = config.sleepMinutes;
    changedAt_ = millis();
    // Not when woken from deep sleep: then the button was pressed to scan.
    if (config.bootLogo && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED)
        drawSplash();
    else
        draw(true);
}

void Tft::drawSplash() {
    int gap = compact_ ? 10 : 16;
    // Narrower than the logo: drawn smaller.
    float scale = w_ < SPLASH_LOGO_W + 20 ? (w_ - 30) / (float)SPLASH_LOGO_W : 1.0f;
    int logoW = SPLASH_LOGO_W * scale, logoH = SPLASH_LOGO_H * scale;
    int y = (h_ - logoH - gap - 16) / 2;
    lcd_->startWrite();
    lcd_->fillScreen(BG);
    lcd_->drawPng(SPLASH_LOGO_PNG, sizeof(SPLASH_LOGO_PNG), (w_ - logoW) / 2, y, 0, 0, 0, 0, scale, scale);
    lcd_->setFont(SMALL);
    lcd_->setTextColor(MUTED, BG);
    lcd_->setTextDatum(textdatum_t::top_center);
    lcd_->drawString(FIRMWARE_VERSION, w_ / 2, y + logoH + gap);
    lcd_->setTextDatum(textdatum_t::top_left);
    lcd_->endWrite();
    splashUntil_ = millis() + SPLASH_SHOWN;
}

void Tft::wake() {
    changedAt_ = millis();
    if (asleep_ && lcd_) {
        lcd_->setBrightness(BRIGHTNESS);
        asleep_ = false;
    }
}

void Tft::off() {
    if (lcd_) lcd_->setBrightness(0);
    asleep_ = true;
}

bool Tft::readTouch(int &x, int &y) {
    int rawX, rawY;
#ifdef BOARD_C6_TOUCH147
    if (!axsRead(rawX, rawY)) return false;
    // Portrait points: in landscape (rotation 1) their Y runs across the
    // screen and their X down.
    x = constrain(rawY, 0, w_ - 1);
    y = constrain(rawX, 0, h_ - 1);
#else
    if (!touchRaw(rawX, rawY)) return false;
    // In landscape the controller's Y runs across the screen and its X down.
    x = constrain(map(rawY, TOUCH_X0, TOUCH_X1, 0, w_ - 1), 0, w_ - 1);
    y = constrain(map(rawX, TOUCH_Y0, TOUCH_Y1, 0, h_ - 1), 0, h_ - 1);
#endif
    if (flip_) {
        x = w_ - 1 - x;
        y = h_ - 1 - y;
    }
#ifdef TOUCH_DEBUG
    Serial.printf("touch raw %d %d -> %d %d\n", rawX, rawY, x, y);
#endif
    return true;
}

void Tft::loop() {
    if (!lcd_) return;
    int x, y;
    bool down = touch_ && readTouch(x, y);
    if (down && !touchDown_ && millis() - lastTouch_ > TOUCH_REPEAT) {
        lastTouch_ = millis();
        app.usedAt = lastTouch_;
        // A touch on a dark screen only wakes it.
        if (asleep_)
            wake();
        else if (splashUntil_)
            draw(true);     // a touch on the logo skips it
        else
            touched(x, y);
        changedAt_ = millis();
    }
    touchDown_ = down;
    if (millis() - lastDraw_ >= DRAW_INTERVAL) draw(false);
    if (scrolling_ && !asleep_ && millis() - lastScroll_ >= SCROLL_STEP) drawScroll();
}

// Scroll text that does not fit width (in the current font) at x, y.
// False when there is no memory for it (then it is cut instead).
bool Tft::startScroll(const String &text, int x, int y, int width, uint32_t color) {
    if (!sprite_) {
        sprite_ = new LGFX_Sprite(lcd_);
        sprite_->setColorDepth(16);
    }
    const lgfx::IFont *font = lcd_->getFont();
    int height = lcd_->fontHeight();
    if (sprite_->width() != width || sprite_->height() != height) {
        sprite_->deleteSprite();
        if (!sprite_->createSprite(width, height)) return false;
    }
    sprite_->setFont(font);
    sprite_->setTextColor(color, BG);
    sprite_->setTextDatum(textdatum_t::top_left);
    // The same name again (a redraw for something else): carry on.
    if (text != scrollText_) {
        scrollText_ = text;
        scrollStart_ = millis();
    }
    scrollX_ = x;
    scrollY_ = y;
    scrolling_ = true;
    drawScroll();
    return true;
}

void Tft::drawScroll() {
    lastScroll_ = millis();
    int span = sprite_->textWidth(scrollText_.c_str()) + SCROLL_GAP;
    uint32_t t = (millis() - scrollStart_) % (SCROLL_PAUSE + span * SCROLL_STEP);
    int offset = t < SCROLL_PAUSE ? 0 : (t - SCROLL_PAUSE) / SCROLL_STEP;
    sprite_->fillScreen(BG);
    sprite_->drawString(scrollText_.c_str(), -offset, 0);
    sprite_->drawString(scrollText_.c_str(), span - offset, 0);
    sprite_->pushSprite(scrollX_, scrollY_);
}

void Tft::touched(int x, int y) {
    Screen screen = current();
    if (screen == Screen::Setup) return;
    Config &config = app.config;
    if (y < header_) {
        // The next printer.
        if (config.printers.size() < 2) return;
        config.active = (config.active + 1) % config.printers.size();
        saveConfig(config);
        app.feedback.play(Signal::Read);
        return;
    }
    if (config.active < 0 || config.active >= (int)config.printers.size()) return;
    PrinterConfig &printer = config.printers[config.active];
    // A short screen shows a scan without the slot row (its pick buttons
    // take that room).
    bool slotRow = !compact_ || screen != Screen::Scan;
    if (y >= footer_ && slotRow) {
        // The fixed slot for this reader.
        int first = slotRowFirst(printer), index = (x - corner_) / slotWidth();
        int slot = first + index;
        if (!printer.hasSlots() || index > SLOT_COUNT || slot > printer.slotCount() || slot == printer.slot) return;
        printer.slot = slot;
        saveConfig(config);
        app.feedback.count(slot);
        return;
    }
    if (screen == Screen::Scan && app.last.waiting && y >= pickY() && y < pickY() + pickH()) {
        int slot = (x - PICK_X - corner_) / (pickW() + 2) + 1;
        int target = app.last.printer >= 0 ? app.last.printer : config.active;
        if (slot < 1 || slot > config.printers[target].slotCount()) return;
        app.last.status = "Sending to " + slotName(config.printers[target], slot) + "...";
        app.last.waiting = false;
        draw(true);
        // Failed: the slot buttons come back for another try.
        if (!app.assignLast(target, slot).ok) app.last.waiting = true;
    }
}

Tft::Screen Tft::current() const {
    if (app.apMode) return Screen::Setup;
    if (scanShown()) return Screen::Scan;
    if (!app.reader.connected()) return Screen::NoReader;
    return Screen::Idle;
}

String Tft::contents(Screen screen) const {
    String s = String((int)screen) + "|" + address();
    if (app.voiceShown) s += "|voice " + app.voiceText;
    if (screen == Screen::Setup) return s + "|" + app.apName + "|" + app.wifiError;
    const PrinterConfig *printer = app.config.activePrinter();
    if (printer) s += "|" + printer->name + "|" + String(printer->model == Model::Creator5) + String(printer->slot);
    s += "|" + String(app.config.printers.size());
    if (screen == Screen::NoReader) return s + "|" + app.reader.error();
    if (screen == Screen::Scan) {
        const ScanRecord &last = app.last;
        s += "|" + String(last.id) + String(last.ok) + String(last.waiting) + "|" + last.status + "|" +
             last.printerTitle + "|" + String(last.printerSpool);
    }
    return s;
}

void Tft::draw(bool force) {
    if (!lcd_) return;
    if (splashUntil_) {
        if (!force && (int32_t)(millis() - splashUntil_) < 0) return;
        splashUntil_ = 0;
        force = true;
    }
    lastDraw_ = millis();
    Screen screen = current();
    String now = contents(screen);
    if (!force && now == shown_) {
        if (!asleep_ && sleepMinutes_ > 0 && millis() - changedAt_ > (uint32_t)sleepMinutes_ * 60000) {
            lcd_->setBrightness(0);
            asleep_ = true;
        }
        return;
    }
    shown_ = now;
    wake();
    scrolling_ = false;     // drawScan starts it again for a long name
    lcd_->startWrite();
    lcd_->fillScreen(BG);
    switch (screen) {
    case Screen::Setup: drawSetup(); break;
    case Screen::NoReader: drawNoReader(); break;
    case Screen::Idle: drawIdle(); break;
    case Screen::Scan: drawScan(); break;
    }
    if (screen != Screen::Setup) {
        drawHeader();
        // A short screen needs the slot row's room for a scan's status.
        if (!compact_ || screen != Screen::Scan) drawSlots();
    }
    if (app.voiceShown) drawVoice();
    lcd_->endWrite();
}

// A voice command's prompt or result, over the bottom row.
void Tft::drawVoice() {
    auto &lcd = *lcd_;
    int y = footer_, x = corner_ + 3, w = w_ - 2 * corner_ - 6, h = h_ - footer_ - 4 - corner_ / 2;
    lcd.fillRect(0, footer_ - 1, w_, h_ - footer_ + 1, BG);
    lcd.fillRoundRect(x, y + 4, w, h, 6, ACCENT);
    lcd.setTextColor(TEXT, ACCENT);
    const String &text = app.voiceText;
    lcd.setFont(BODY_BOLD);
    if (lcd.textWidth(text.c_str()) > w - 12) lcd.setFont(SMALL);
    String shown = fit(lcd, text, w - 12);
    lcd.drawString(shown.c_str(), x + (w - lcd.textWidth(shown.c_str())) / 2, y + 4 + (h - lcd.fontHeight()) / 2);
}

// Printer name (tap: next printer) and the reader's address.
void Tft::drawHeader() {
    auto &lcd = *lcd_;
    int dy = tiny_ ? -8 : compact_ ? -4 : 0;
    lcd.fillRect(0, 0, w_, header_, PANEL);
    lcd.setFont(SMALL);
    lcd.setTextColor(MUTED, PANEL);
    String ip = address();
    int ipWidth = lcd.textWidth(ip.c_str());
    lcd.drawString(ip.c_str(), w_ - 8 - corner_ - ipWidth, 13 + dy);
    const PrinterConfig *printer = app.config.activePrinter();
    lcd.setFont(tiny_ ? BODY_BOLD : TITLE);
    lcd.setTextColor(printer ? TEXT : WARN, PANEL);
    int x = 10 + corner_;
    if (touch_ && app.config.printers.size() > 1) {
        // A "more" mark: tap for the next printer.
        lcd.fillTriangle(x, 14, x + 12, 14, x + 6, 24, MUTED);
        x += 20;
    }
    String name = printer ? printer->name : String("No printer");
    lcd.drawString(fit(lcd, name, w_ - x - ipWidth - 18).c_str(), x, tiny_ ? 4 : 9 + dy);
}

// The fixed slot: Ask, 1-4 (T0-T3 on the Creator 5, A1-A4 etc. on a Bambu
// AMS, gate/lane/tool numbers on Klipper).
void Tft::drawSlots() {
    auto &lcd = *lcd_;
    const PrinterConfig *printer = app.config.activePrinter();
    // Nothing for Spoolman (or no printer); else only the slots it has, and
    // Ask only where the printer's screen asks.
    if (!printer || !printer->hasSlots()) return;
    int slotW = slotWidth();
    lcd.drawFastHLine(0, footer_ - 1, w_, LINE);
    lcd.setFont(BODY_BOLD);
    for (int slot = slotRowFirst(*printer), x = corner_; slot <= printer->slotCount(); slot++, x += slotW) {
        bool on = printer->slot == slot;
        uint32_t fill = on ? ACCENT : PANEL;
        lcd.fillRoundRect(x + 3, footer_ + 4, slotW - 6, h_ - footer_ - 8 - corner_ / 2, 6, fill);
        String label = slotShortName(*printer, slot);
        lcd.setTextColor(TEXT, fill);
        int w = lcd.textWidth(label.c_str());
        lcd.drawString(label.c_str(), x + (slotW - w) / 2, footer_ + (tiny_ ? 7 : compact_ ? 9 : 15));
    }
}

void Tft::drawSetup() {
    auto &lcd = *lcd_;
    // Rows: title, "join", network, password, address, error.
    static const int FULL[] = {14, 64, 86, 112, 136, 170}, COMPACT[] = {6, 42, 62, 86, 108, 132},
                     TINY[] = {4, 32, 50, 72, 92, 114};
    const int *y = tiny_ ? TINY : compact_ ? COMPACT : FULL;
    const int mx = margin();
    lcd.setFont(tiny_ ? TITLE : BIG);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString("Wi-Fi setup", mx, y[0]);
    lcd.setFont(BODY);
    lcd.setTextColor(MUTED, BG);
    lcd.drawString("On a phone or PC, join", mx, y[1]);
    lcd.setFont(BODY_BOLD);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString(app.apName.c_str(), mx, y[2]);
    lcd.setFont(BODY);
    // The values line up just past the longer label.
    const int vx = tiny_ ? mx + max(lcd.textWidth("password"), lcd.textWidth("browse to")) + 8 : 100;
    lcd.setTextColor(MUTED, BG);
    lcd.drawString("password", mx, y[3]);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString("lanetag1", vx, y[3]);
    lcd.setTextColor(MUTED, BG);
    lcd.drawString("browse to", mx, y[4]);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString(("http://" + WiFi.softAPIP().toString()).c_str(), vx, y[4]);
    if (app.wifiError.length()) {
        lcd.setFont(SMALL);
        lcd.setTextColor(WARN, BG);
        String error = app.config.wifiSsid + ": " + app.wifiError;
        // One line on a tiny screen: scrolls when it does not fit.
        if (!tiny_ || lcd.textWidth(error.c_str()) <= w_ - 2 * mx || !startScroll(error, mx, y[5], w_ - 2 * mx, WARN))
            wrap(error, mx, y[5], w_ - 2 * mx, tiny_ ? 1 : compact_ ? 2 : 3);
    }
}

void Tft::drawNoReader() {
    auto &lcd = *lcd_;
    lcd.setFont(BIG);
    lcd.setTextColor(BAD, BG);
    lcd.drawString("No reader", margin(), header_ + (tiny_ ? 2 : compact_ ? 8 : 14));
    lcd.setFont(SMALL);
    lcd.setTextColor(MUTED, BG);
    wrap(app.reader.error(), margin(), header_ + (tiny_ ? 38 : compact_ ? 46 : 58), w_ - 2 * margin(),
         tiny_ ? 2 : compact_ ? 3 : 5);
}

void Tft::drawIdle() {
    auto &lcd = *lcd_;
    // Rows below the header: "Ready", the hint, the slot, the reader.
    static const int FULL[] = {16, 62, 92, 114}, COMPACT[] = {6, 40, 62, 80}, TINY[] = {0, 32, 52, 66};
    const int *y = tiny_ ? TINY : compact_ ? COMPACT : FULL;
    lcd.setFont(BIG);
    lcd.setTextColor(GOOD, BG);
    const int mx = margin();
    lcd.drawString("Ready", mx, header_ + y[0]);
    lcd.setFont(BODY);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString("Hold a spool to the reader", mx, header_ + y[1]);
    lcd.setFont(SMALL);
    lcd.setTextColor(MUTED, BG);
    const PrinterConfig *printer = app.config.activePrinter();
    String hint = !printer                ? String("Add a printer on the web page")
                  : printer->slot == 0    ? String("The slot is asked for after each scan")
                                          : "Scans go straight into " + slotName(*printer, printer->slot);
    lcd.drawString(hint.c_str(), mx, header_ + y[2]);
    lcd.drawString(app.reader.describe().c_str(), mx, header_ + y[3]);
}

// The last scan:
//   [color]  PLA Silk                  #14
//   [swatch]  Polymaker
//             orange  #FF8000  220 C
//   Status, or the slots to put it in.
void Tft::drawScan() {
    auto &lcd = *lcd_;
    const ScanRecord &last = app.last;
    const Filament &f = last.filament;
    String name, brand, detail;
    if (f.hasFilamentData()) {
        name = f.material;
        if (f.modifiers.length()) name += " " + f.modifiers;
        brand = f.brand.length() ? f.brand : f.name;
        if (f.color >= 0) detail = String(colorName(f.color)) + "  #" + f.colorHex();
    } else if (last.printerTitle.length()) {
        // FF_NFC decodes formats the reader does not know.
        name = last.printerTitle;
    } else {
        name = f.spoolId ? "Spool tag" : "Unknown tag";
        brand = tagKindName(last.tag.kind);
    }
    if (!name.length()) name = last.printerTitle;
    if (last.printerSpool && last.printerTitle.length()) {
        // The printer found the spool (in Spoolman): its name, not the tag's,
        // with the vendor it starts with on the brand line.
        name = last.printerTitle;
        brand = "";
        int n = f.brand.length();
        if (n && name.length() > (unsigned)n + 1 && name[n] == ' ' && name.substring(0, n).equalsIgnoreCase(f.brand)) {
            brand = name.substring(0, n);
            name = name.substring(n + 1);
        }
    }

    // Color swatch.
    const int sx = tiny_ ? margin() : 10, sy = header_ + (compact_ ? 6 : 10), ss = tiny_ ? 50 : compact_ ? 64 : 76;
    // Text rows beside the swatch: brand, then color and temperature.
    const int brandY = sy + (tiny_ ? 22 : compact_ ? 26 : 32), detailY = sy + (tiny_ ? 38 : compact_ ? 46 : 58);
    if (f.color >= 0) {
        lcd.fillRoundRect(sx, sy, ss, ss, 8, (uint32_t)f.color);
        lcd.drawRoundRect(sx, sy, ss, ss, 8, LINE);
    } else {
        lcd.fillRoundRect(sx, sy, ss, ss, 8, PANEL);
        lcd.setFont(BIG);
        lcd.setTextColor(MUTED, PANEL);
        lcd.drawString("?", sx + (ss - lcd.textWidth("?")) / 2, sy + (ss - 32) / 2);
    }

    const int tx = sx + ss + 10, tw = w_ - tx - 8;
    int spool = last.printerSpool ? last.printerSpool : f.spoolId;
    int spoolWidth = 0;
    lcd.setFont(BODY_BOLD);
    if (spool) {
        String id = "#" + String(spool);
        spoolWidth = lcd.textWidth(id.c_str()) + 8;
        lcd.setTextColor(ACCENT, BG);
        lcd.drawString(id.c_str(), w_ - 8 - spoolWidth + 8, sy + 4);
    }
    uint32_t nameColor = spoolTextColor(f.color);
    lcd.setTextColor(nameColor, BG);
    lcd.setFont(lcd.textWidth(name.c_str()) <= tw - spoolWidth ? TITLE : BODY_BOLD);
    if (lcd.textWidth(name.c_str()) <= tw - spoolWidth || !startScroll(name, tx, sy + 2, tw - spoolWidth, nameColor))
        lcd.drawString(fit(lcd, name, tw - spoolWidth).c_str(), tx, sy + 2);
    lcd.setFont(BODY);
    lcd.setTextColor(MUTED, BG);
    if (brand.length()) lcd.drawString(fit(lcd, brand, tw).c_str(), tx, brandY);
    int nozzle = f.hasFilamentData() ? f.nozzleTemp() : 0;
    if (detail.length() || nozzle) {
        lcd.setFont(SMALL);
        lcd.setTextColor(TEXT, BG);
        int x = tx;
        if (detail.length()) {
            lcd.drawString(detail.c_str(), x, detailY);
            x += lcd.textWidth(detail.c_str()) + 12;
        }
        if (nozzle) {
            String temp = String(nozzle);
            lcd.drawString(temp.c_str(), x, detailY);
            x += lcd.textWidth(temp.c_str()) + 3;
            lcd.drawCircle(x, detailY + 3, 2, TEXT);   // degree sign
            lcd.drawString("C", x + 5, detailY);
        }
    }

    const PrinterConfig *target = last.printer >= 0 && last.printer < (int)app.config.printers.size()
                                      ? &app.config.printers[last.printer]
                                      : nullptr;
    String status = last.status;
    lcd.setFont(SMALL);
    if (last.waiting && target && !touch_) {
        lcd.setTextColor(WARN, BG);
        wrap(target->method == Method::FfNfc ? "Pick a slot on the printer or the web page"
                                               : "Pick a slot on the web page",
             10, sy + ss + 6, w_ - 20, tiny_ ? 2 : 3);
        return;
    }
    if (last.waiting && target) {
        lcd.setTextColor(WARN, BG);
        String where = target->method == Method::FfNfc ? "Pick a slot here or on the printer:"
                                                         : "Pick a slot here or on the web page:";
        lcd.drawString(where.c_str(), 10, pickY() - 20);
        lcd.setFont(TITLE);
        for (int slot = 1; slot <= target->slotCount(); slot++) {
            int x = PICK_X + corner_ + (slot - 1) * (pickW() + 2);
            lcd.fillRoundRect(x, pickY(), pickW(), pickH(), 6, WARN);
            String label = slotShortName(*target, slot);
            lcd.setTextColor(BG, WARN);
            lcd.drawString(label.c_str(), x + (pickW() - lcd.textWidth(label.c_str())) / 2,
                           pickY() + (pickH() - lcd.fontHeight()) / 2);
        }
        return;
    }
    lcd.setTextColor(!last.ok ? BAD : status.startsWith("Sending") ? MUTED : GOOD, BG);
    wrap(status, 10, sy + ss + (tiny_ ? 6 : compact_ ? 8 : 10), w_ - 20, tiny_ ? 2 : 3);
}

int Tft::wrap(const String &text, int x, int y, int width, int maxLines) {
    auto &lcd = *lcd_;
    int lineHeight = lcd.fontHeight() + 2;
    int start = 0, lines = 0;
    while (start < (int)text.length() && lines < maxLines) {
        // The longest run of whole words that fits.
        int end = start, fitAt = -1;
        while (end <= (int)text.length()) {
            if (end == (int)text.length() || text[end] == ' ') {
                if (lcd.textWidth(text.substring(start, end).c_str()) > width) break;
                fitAt = end;
                if (end == (int)text.length()) break;
            }
            end++;
        }
        if (fitAt < 0) {
            // One word wider than the line: cut it.
            fitAt = start + 1;
            while (fitAt < (int)text.length() && lcd.textWidth(text.substring(start, fitAt + 1).c_str()) <= width)
                fitAt++;
        }
        String line = text.substring(start, fitAt);
        if (lines == maxLines - 1 && fitAt < (int)text.length()) line = fit(lcd, line + " " + text.substring(fitAt), width);
        lcd.drawString(line.c_str(), x, y);
        y += lineHeight;
        lines++;
        start = fitAt;
        while (start < (int)text.length() && text[start] == ' ') start++;
    }
    return y;
}

#endif
