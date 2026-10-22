#ifdef HAS_TFT

#include "tft.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>

#include "app.h"

// The CYD's screen: HSPI, backlight on GPIO 21.
class CydLcd : public lgfx::LGFX_Device {
public:
    explicit CydLcd(bool st7789) {
        lgfx::Panel_Device *panel = st7789 ? (lgfx::Panel_Device *)&st7789_ : &ili9341_;
        {
            auto c = bus_.config();
            c.spi_host = HSPI_HOST;
            c.spi_mode = 0;
            c.freq_write = 40000000;
            c.freq_read = 16000000;
            c.spi_3wire = false;
            c.use_lock = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = 14;
            c.pin_mosi = 13;
            c.pin_miso = 12;
            c.pin_dc = 2;
            bus_.config(c);
            panel->setBus(&bus_);
        }
        {
            auto c = panel->config();
            c.pin_cs = 15;
            c.pin_rst = -1;
            c.pin_busy = -1;
            c.panel_width = 240;
            c.panel_height = 320;
            c.memory_width = 240;
            c.memory_height = 320;
            c.readable = true;
            c.invert = false;
            c.rgb_order = false;
            c.bus_shared = false;
            panel->config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = 21;
            c.invert = false;
            c.freq = 12000;
            c.pwm_channel = 7;
            light_.config(c);
            panel->setLight(&light_);
        }
        setPanel(panel);
    }

private:
    lgfx::Panel_ILI9341 ili9341_;
    lgfx::Panel_ST7789 st7789_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

namespace {

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

}  // namespace

namespace {

constexpr int W = 320, H = 240;
constexpr int HEADER = 40;
constexpr int FOOTER = 192;            // top of the slot buttons
constexpr int SLOT_W = W / 5;          // Ask, 1-4
constexpr int PICK_Y = 146, PICK_H = 42, PICK_W = 74, PICK_X = 8;
constexpr uint32_t SCAN_SHOWN = 5 * 60 * 1000;
constexpr uint32_t DRAW_INTERVAL = 200;
constexpr uint32_t TOUCH_REPEAT = 250;
constexpr uint8_t BRIGHTNESS = 200;

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
    lcd_ = new CydLcd(config.type == DisplayType::CydSt7789);
    lcd_->init();
    touchBegin();
    flip_ = config.flip;
    lcd_->setRotation(flip_ ? 3 : 1);
    lcd_->setBrightness(BRIGHTNESS);
    lcd_->setTextDatum(textdatum_t::top_left);
    sleepMinutes_ = config.sleepMinutes;
    changedAt_ = millis();
    draw(true);
}

void Tft::wake() {
    changedAt_ = millis();
    if (asleep_ && lcd_) {
        lcd_->setBrightness(BRIGHTNESS);
        asleep_ = false;
    }
}

bool Tft::readTouch(int &x, int &y) {
    int rawX, rawY;
    if (!touchRaw(rawX, rawY)) return false;
    // In landscape the controller's Y runs across the screen and its X down.
    x = constrain(map(rawY, TOUCH_X0, TOUCH_X1, 0, W - 1), 0, W - 1);
    y = constrain(map(rawX, TOUCH_Y0, TOUCH_Y1, 0, H - 1), 0, H - 1);
    if (flip_) {
        x = W - 1 - x;
        y = H - 1 - y;
    }
#ifdef TOUCH_DEBUG
    Serial.printf("touch raw %d %d -> %d %d\n", rawX, rawY, x, y);
#endif
    return true;
}

void Tft::loop() {
    if (!lcd_) return;
    int x, y;
    bool down = readTouch(x, y);
    if (down && !touchDown_ && millis() - lastTouch_ > TOUCH_REPEAT) {
        lastTouch_ = millis();
        // A touch on a dark screen only wakes it.
        if (asleep_)
            wake();
        else
            touched(x, y);
        changedAt_ = millis();
    }
    touchDown_ = down;
    if (millis() - lastDraw_ >= DRAW_INTERVAL) draw(false);
}

void Tft::touched(int x, int y) {
    Screen screen = current();
    if (screen == Screen::Setup) return;
    Config &config = app.config;
    if (y < HEADER) {
        // The next printer.
        if (config.printers.size() < 2) return;
        config.active = (config.active + 1) % config.printers.size();
        saveConfig(config);
        app.feedback.play(Signal::Read);
        return;
    }
    if (config.active < 0 || config.active >= (int)config.printers.size()) return;
    PrinterConfig &printer = config.printers[config.active];
    if (y >= FOOTER) {
        // The fixed slot for this reader.
        int slot = constrain(x / SLOT_W, 0, SLOT_COUNT);
        if (slot == printer.slot) return;
        printer.slot = slot;
        saveConfig(config);
        app.feedback.count(slot);
        return;
    }
    if (screen == Screen::Scan && app.last.waiting && y >= PICK_Y && y < PICK_Y + PICK_H) {
        int slot = (x - PICK_X) / (PICK_W + 2) + 1;
        if (slot < 1 || slot > SLOT_COUNT) return;
        int target = app.last.printer >= 0 ? app.last.printer : config.active;
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
        drawSlots();
    }
    lcd_->endWrite();
}

// Printer name (tap: next printer) and the reader's address.
void Tft::drawHeader() {
    auto &lcd = *lcd_;
    lcd.fillRect(0, 0, W, HEADER, PANEL);
    lcd.setFont(SMALL);
    lcd.setTextColor(MUTED, PANEL);
    String ip = address();
    int ipWidth = lcd.textWidth(ip.c_str());
    lcd.drawString(ip.c_str(), W - 8 - ipWidth, 13);
    const PrinterConfig *printer = app.config.activePrinter();
    lcd.setFont(TITLE);
    lcd.setTextColor(printer ? TEXT : WARN, PANEL);
    int x = 10;
    if (app.config.printers.size() > 1) {
        // A "more" mark: tap for the next printer.
        lcd.fillTriangle(10, 14, 22, 14, 16, 24, MUTED);
        x = 30;
    }
    String name = printer ? printer->name : String("No printer");
    lcd.drawString(fit(lcd, name, W - x - ipWidth - 18).c_str(), x, 9);
}

// The fixed slot: Ask, 1-4 (T0-T3 on the Creator 5).
void Tft::drawSlots() {
    auto &lcd = *lcd_;
    const PrinterConfig *printer = app.config.activePrinter();
    lcd.drawFastHLine(0, FOOTER - 1, W, LINE);
    lcd.setFont(BODY_BOLD);
    for (int slot = 0; slot <= SLOT_COUNT; slot++) {
        int x = slot * SLOT_W;
        bool on = printer && printer->slot == slot;
        uint32_t fill = on ? ACCENT : PANEL;
        lcd.fillRoundRect(x + 3, FOOTER + 4, SLOT_W - 6, H - FOOTER - 8, 6, fill);
        String label = !printer ? (slot ? String(slot) : String("Ask"))
                       : slot == 0 ? String("Ask")
                       : printer->model == Model::Creator5 ? "T" + String(slot - 1)
                                                           : String(slot);
        lcd.setTextColor(printer ? TEXT : MUTED, fill);
        int w = lcd.textWidth(label.c_str());
        lcd.drawString(label.c_str(), x + (SLOT_W - w) / 2, FOOTER + 15);
    }
}

void Tft::drawSetup() {
    auto &lcd = *lcd_;
    lcd.setFont(BIG);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString("Wi-Fi setup", 14, 14);
    lcd.setFont(BODY);
    lcd.setTextColor(MUTED, BG);
    lcd.drawString("On a phone or PC, join", 14, 64);
    lcd.setFont(BODY_BOLD);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString(app.apName.c_str(), 14, 86);
    lcd.setFont(BODY);
    lcd.setTextColor(MUTED, BG);
    lcd.drawString("password", 14, 112);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString("lanetag1", 100, 112);
    lcd.setTextColor(MUTED, BG);
    lcd.drawString("then open", 14, 136);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString(("http://" + WiFi.softAPIP().toString()).c_str(), 100, 136);
    if (app.wifiError.length()) {
        lcd.setFont(SMALL);
        lcd.setTextColor(WARN, BG);
        wrap(app.config.wifiSsid + ": " + app.wifiError, 14, 170, W - 28, 3);
    }
}

void Tft::drawNoReader() {
    auto &lcd = *lcd_;
    lcd.setFont(BIG);
    lcd.setTextColor(BAD, BG);
    lcd.drawString("No reader", 14, HEADER + 14);
    lcd.setFont(SMALL);
    lcd.setTextColor(MUTED, BG);
    wrap(app.reader.error(), 14, HEADER + 58, W - 28, 5);
}

void Tft::drawIdle() {
    auto &lcd = *lcd_;
    lcd.setFont(BIG);
    lcd.setTextColor(GOOD, BG);
    lcd.drawString("Ready", 14, HEADER + 16);
    lcd.setFont(BODY);
    lcd.setTextColor(TEXT, BG);
    lcd.drawString("Hold a spool to the reader", 14, HEADER + 62);
    lcd.setFont(SMALL);
    lcd.setTextColor(MUTED, BG);
    const PrinterConfig *printer = app.config.activePrinter();
    String hint = !printer                ? String("Add a printer on the web page")
                  : printer->slot == 0    ? String("The slot is asked for after each scan")
                                          : "Scans go straight into " + slotName(*printer, printer->slot);
    lcd.drawString(hint.c_str(), 14, HEADER + 92);
    lcd.drawString(app.reader.describe().c_str(), 14, HEADER + 114);
}

// The last scan:
//   [colour]  PLA Silk                  #14
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
        // zmod_nfc decodes formats the reader does not know.
        name = last.printerTitle;
    } else {
        name = f.spoolId ? "Spool tag" : "Unknown tag";
        brand = tagKindName(last.tag.kind);
    }
    if (!name.length()) name = last.printerTitle;

    // Colour swatch.
    const int sx = 10, sy = HEADER + 10, ss = 76;
    if (f.color >= 0) {
        lcd.fillRoundRect(sx, sy, ss, ss, 8, (uint32_t)f.color);
        lcd.drawRoundRect(sx, sy, ss, ss, 8, LINE);
    } else {
        lcd.fillRoundRect(sx, sy, ss, ss, 8, PANEL);
        lcd.setFont(BIG);
        lcd.setTextColor(MUTED, PANEL);
        lcd.drawString("?", sx + (ss - lcd.textWidth("?")) / 2, sy + 22);
    }

    const int tx = sx + ss + 10, tw = W - tx - 8;
    int spool = last.printerSpool ? last.printerSpool : f.spoolId;
    int spoolWidth = 0;
    lcd.setFont(BODY_BOLD);
    if (spool) {
        String id = "#" + String(spool);
        spoolWidth = lcd.textWidth(id.c_str()) + 8;
        lcd.setTextColor(ACCENT, BG);
        lcd.drawString(id.c_str(), W - 8 - spoolWidth + 8, sy + 4);
    }
    lcd.setTextColor(TEXT, BG);
    lcd.setFont(lcd.textWidth(name.c_str()) <= tw - spoolWidth ? TITLE : BODY_BOLD);
    lcd.drawString(fit(lcd, name, tw - spoolWidth).c_str(), tx, sy + 2);
    lcd.setFont(BODY);
    lcd.setTextColor(MUTED, BG);
    if (brand.length()) lcd.drawString(fit(lcd, brand, tw).c_str(), tx, sy + 32);
    int nozzle = f.hasFilamentData() ? f.nozzleTemp() : 0;
    if (detail.length() || nozzle) {
        lcd.setFont(SMALL);
        lcd.setTextColor(TEXT, BG);
        int x = tx;
        if (detail.length()) {
            lcd.drawString(detail.c_str(), x, sy + 58);
            x += lcd.textWidth(detail.c_str()) + 12;
        }
        if (nozzle) {
            String temp = String(nozzle);
            lcd.drawString(temp.c_str(), x, sy + 58);
            x += lcd.textWidth(temp.c_str()) + 3;
            lcd.drawCircle(x, sy + 61, 2, TEXT);       // degree sign
            lcd.drawString("C", x + 5, sy + 58);
        }
    }

    const PrinterConfig *target = last.printer >= 0 && last.printer < (int)app.config.printers.size()
                                      ? &app.config.printers[last.printer]
                                      : nullptr;
    String status = last.status;
    lcd.setFont(SMALL);
    if (last.waiting && target) {
        lcd.setTextColor(WARN, BG);
        String where = target->method == Method::ZmodNfc ? "Pick a slot here or on the printer:"
                                                         : "Pick a slot here or on the web page:";
        lcd.drawString(where.c_str(), 10, PICK_Y - 20);
        lcd.setFont(TITLE);
        for (int slot = 1; slot <= SLOT_COUNT; slot++) {
            int x = PICK_X + (slot - 1) * (PICK_W + 2);
            lcd.fillRoundRect(x, PICK_Y, PICK_W, PICK_H, 6, WARN);
            String label = target->model == Model::Creator5 ? "T" + String(slot - 1) : String(slot);
            lcd.setTextColor(BG, WARN);
            lcd.drawString(label.c_str(), x + (PICK_W - lcd.textWidth(label.c_str())) / 2, PICK_Y + 10);
        }
        return;
    }
    lcd.setTextColor(!last.ok ? BAD : status.startsWith("Sending") ? MUTED : GOOD, BG);
    wrap(status, 10, sy + ss + 10, W - 20, 3);
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
