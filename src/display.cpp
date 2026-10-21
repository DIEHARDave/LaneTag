#include "display.h"

#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>
#include <soc/soc_caps.h>

#include "app.h"

namespace {

// The bus the display is on: Wire when it shares the PN532's pins (or the
// reader is not on I2C), else Wire1.
TwoWire *bus = nullptr;

// A scan stays on the screen this long (unless it waits for a slot).
constexpr uint32_t SCAN_SHOWN = 5 * 60 * 1000;
constexpr uint32_t SLOT_SHOWN = 3000;
constexpr uint32_t DRAW_INTERVAL = 200;
constexpr int WIDTH = 128;

const uint8_t *const SMALL = u8g2_font_5x7_tf;
const uint8_t *const MEDIUM = u8g2_font_6x10_tf;
const uint8_t *const LARGE = u8g2_font_helvB12_tf;
const uint8_t *const HUGE_ = u8g2_font_helvB24_tf;

uint8_t byteCallback(u8x8_t *u8x8, uint8_t msg, uint8_t count, void *data) {
    switch (msg) {
    case U8X8_MSG_BYTE_SEND: bus->write((uint8_t *)data, count); break;
    case U8X8_MSG_BYTE_START_TRANSFER: bus->beginTransmission(u8x8_GetI2CAddress(u8x8) >> 1); break;
    case U8X8_MSG_BYTE_END_TRANSFER: bus->endTransmission(); break;
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC: break;
    default: return 0;
    }
    return 1;
}

// A colour name for a monochrome screen: the nearest of a few common
// filament colours by the "redmean" distance.
struct NamedColor { const char *name; uint32_t rgb; };
const NamedColor COLOR_NAMES[] = {
    {"white", 0xFFFFFF}, {"black", 0x000000}, {"grey", 0x808080}, {"light grey", 0xC0C0C0},
    {"dark grey", 0x404040}, {"red", 0xE02020}, {"dark red", 0x801010}, {"orange", 0xFF8000},
    {"yellow", 0xFFE020}, {"gold", 0xC8A040}, {"beige", 0xE8D8B0}, {"brown", 0x804820},
    {"green", 0x20B040}, {"lime", 0xA0E040}, {"dark green", 0x105828}, {"teal", 0x109090},
    {"cyan", 0x40D8F0}, {"light blue", 0x80B8F0}, {"blue", 0x2050E0}, {"navy", 0x182868},
    {"purple", 0x7030B0}, {"violet", 0xB060F0}, {"magenta", 0xF030C0}, {"pink", 0xF8A0C0},
};

const char *colorName(uint32_t color) {
    long r1 = (color >> 16) & 0xFF, g1 = (color >> 8) & 0xFF, b1 = color & 0xFF;
    const char *best = "";
    double bestDistance = 1e18;
    for (auto &c : COLOR_NAMES) {
        long r2 = (c.rgb >> 16) & 0xFF, g2 = (c.rgb >> 8) & 0xFF, b2 = c.rgb & 0xFF;
        double mean = (r1 + r2) / 2.0;
        double d = (2 + mean / 256) * (r1 - r2) * (r1 - r2) + 4 * (g1 - g2) * (g1 - g2) +
                   (2 + (255 - mean) / 256) * (b1 - b2) * (b1 - b2);
        if (d < bestDistance) {
            bestDistance = d;
            best = c.name;
        }
    }
    return best;
}

String slotName(const PrinterConfig &printer, int slot) {
    if (slot <= 0) return "Ask";
    // As the printers name them: toolheads T0-T3, IFS slots 1-4.
    return printer.model == Model::Creator5 ? "T" + String(slot - 1) : "Slot " + String(slot);
}

}  // namespace

const char *displayTypeName(DisplayType type) {
    switch (type) {
    case DisplayType::SSD1306: return "ssd1306";
    case DisplayType::SH1106: return "sh1106";
    default: return "none";
    }
}

void Display::begin(const DisplayConfig &config, const ReaderPins &reader) {
    if (config.type == DisplayType::None) return;
    if (config.sda < 0 || config.scl < 0) {
        error_ = "the display needs its SDA and SCL pins";
        return;
    }
    bool readerI2C = reader.chip == ReaderChip::PN532 && reader.bus == Pn532Bus::I2C;
    if (readerI2C && reader.sda == config.sda && reader.scl == config.scl) {
        // Already started by the PN532 (at 100 kHz, which both are fine with).
        bus = &Wire;
    } else if (!readerI2C) {
        bus = &Wire;
        bus->begin(config.sda, config.scl, 400000);
    } else {
#if SOC_I2C_NUM > 1
        bus = &Wire1;
        bus->begin(config.sda, config.scl, 400000);
#else
        error_ = "this chip has one I2C bus: put the display on the reader's SDA and SCL pins";
        return;
#endif
    }
    bus->beginTransmission(config.address);
    if (bus->endTransmission() != 0) {
        char text[96];
        snprintf(text, sizeof(text), "no display answering at 0x%02X (SDA %d, SCL %d)", config.address,
                 config.sda, config.scl);
        error_ = text;
        log_w("%s", text);
        return;
    }
    u8g2_ = new U8G2();
    const u8g2_cb_t *rotation = config.flip ? U8G2_R2 : U8G2_R0;
    if (config.type == DisplayType::SH1106)
        u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2_->getU8g2(), rotation, byteCallback, u8x8_gpio_and_delay_arduino);
    else
        u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_->getU8g2(), rotation, byteCallback, u8x8_gpio_and_delay_arduino);
    u8g2_->setI2CAddress(config.address << 1);
    u8g2_->begin();
    u8g2_->enableUTF8Print();
    sleepMinutes_ = config.sleepMinutes;
    memset(shown_, 0xFF, sizeof(shown_));
    changedAt_ = millis();
    draw(true);
}

void Display::showSlot() {
    slotUntil_ = millis() + SLOT_SHOWN;
    wake();
    draw(true);
}

void Display::wake() {
    changedAt_ = millis();
    if (asleep_ && u8g2_) {
        u8g2_->setPowerSave(0);
        asleep_ = false;
    }
}

void Display::loop() {
    if (!u8g2_ || millis() - lastDraw_ < DRAW_INTERVAL) return;
    draw(false);
}

void Display::draw(bool force) {
    if (!u8g2_) return;
    lastDraw_ = millis();
    u8g2_->clearBuffer();
    render();
    // Send only a changed picture: a full frame takes 25-100 ms on I2C.
    uint8_t *buffer = u8g2_->getBufferPtr();
    if (force || memcmp(buffer, shown_, sizeof(shown_)) != 0) {
        memcpy(shown_, buffer, sizeof(shown_));
        wake();
        u8g2_->sendBuffer();
        return;
    }
    // The same picture for a long time: switch off (OLEDs burn in).
    if (!asleep_ && sleepMinutes_ > 0 && millis() - changedAt_ > (uint32_t)sleepMinutes_ * 60000) {
        u8g2_->setPowerSave(1);
        asleep_ = true;
    }
}

void Display::render() {
    if ((int32_t)(millis() - slotUntil_) < 0) return renderSlot();
    if (app.apMode) return renderSetup();
    const ScanRecord &last = app.last;
    if (last.id && (last.waiting || millis() - last.at < SCAN_SHOWN)) return renderScan();
    renderIdle();
}

// Printer and slot, then a line.
void Display::header() {
    const PrinterConfig *printer = app.config.activePrinter();
    u8g2_->setFont(MEDIUM);
    if (!printer) {
        u8g2_->drawUTF8(0, 8, "No printer");
    } else {
        String slot = slotName(*printer, printer->slot);
        int slotWidth = u8g2_->getUTF8Width(slot.c_str());
        u8g2_->drawUTF8(WIDTH - slotWidth, 8, slot.c_str());
        String name = printer->name;
        while (name.length() > 1 && u8g2_->getUTF8Width(name.c_str()) > WIDTH - slotWidth - 6)
            name.remove(name.length() - 1);
        u8g2_->drawUTF8(0, 8, name.c_str());
    }
    u8g2_->drawHLine(0, 10, WIDTH);
}

int Display::wrap(const String &text, int y, int maxLines) {
    u8g2_->setFont(SMALL);
    int start = 0, lines = 0;
    while (start < (int)text.length() && lines < maxLines) {
        // The longest run of whole words that fits.
        int end = start, fit = -1;
        while (end <= (int)text.length()) {
            if (end == (int)text.length() || text[end] == ' ') {
                if (u8g2_->getUTF8Width(text.substring(start, end).c_str()) > WIDTH) break;
                fit = end;
                if (end == (int)text.length()) break;
            }
            end++;
        }
        if (fit < 0) {
            // One word wider than the screen: cut it.
            fit = start + 1;
            while (fit < (int)text.length() && u8g2_->getUTF8Width(text.substring(start, fit + 1).c_str()) <= WIDTH)
                fit++;
        }
        String line = text.substring(start, fit);
        if (lines == maxLines - 1 && fit < (int)text.length()) {
            while (line.length() && u8g2_->getUTF8Width((line + "...").c_str()) > WIDTH) line.remove(line.length() - 1);
            line += "...";
        }
        y += 8;
        u8g2_->drawUTF8(0, y, line.c_str());
        lines++;
        start = fit;
        while (start < (int)text.length() && text[start] == ' ') start++;
    }
    return y;
}

void Display::renderSlot() {
    header();
    const PrinterConfig *printer = app.config.activePrinter();
    if (!printer) return;
    u8g2_->setFont(SMALL);
    u8g2_->drawUTF8(0, 22, printer->slot ? "Scans go to" : "Scans ask for the slot");
    u8g2_->setFont(HUGE_);
    String slot = slotName(*printer, printer->slot);
    u8g2_->drawUTF8((WIDTH - u8g2_->getUTF8Width(slot.c_str())) / 2, 54, slot.c_str());
}

void Display::renderSetup() {
    u8g2_->setFont(MEDIUM);
    u8g2_->drawUTF8(0, 8, "Wi-Fi setup");
    u8g2_->drawHLine(0, 10, WIDTH);
    u8g2_->setFont(SMALL);
    u8g2_->drawUTF8(0, 20, ("Join " + app.apName).c_str());
    u8g2_->drawUTF8(0, 29, "password zmodnfc1");
    u8g2_->drawUTF8(0, 38, ("open " + WiFi.softAPIP().toString()).c_str());
    if (app.wifiError.length()) wrap(app.wifiError, 38, 3);
}

void Display::renderIdle() {
    header();
    int y;
    if (!app.reader.connected()) {
        u8g2_->setFont(LARGE);
        u8g2_->drawUTF8(0, 26, "No reader");
        y = wrap(app.reader.error(), 28, 3);
    } else {
        u8g2_->setFont(LARGE);
        u8g2_->drawUTF8(0, 28, "Ready");
        u8g2_->setFont(SMALL);
        u8g2_->drawUTF8(0, 40, "Hold a spool to the reader");
        y = 40;
    }
    if (y <= 54) {
        u8g2_->setFont(SMALL);
        String address = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("Wi-Fi lost");
        u8g2_->drawUTF8(0, 63, address.c_str());
    }
}

// The filament of the last scan and how it went:
//   PLA Silk                 (material, large)
//   Polymaker        #19     (brand, Spoolman spool)
//   orange FF8000  220C      (colour, nozzle)
//   Assigned to T1 ...       (status, small)
void Display::renderScan() {
    header();
    const ScanRecord &last = app.last;
    const Filament &f = last.filament;
    String title, brand, detail;
    if (f.hasFilamentData()) {
        title = f.material;
        if (f.modifiers.length()) title += " " + f.modifiers;
        brand = f.brand.length() ? f.brand : f.name;
        if (f.color >= 0) detail = String(colorName(f.color)) + " " + f.colorHex();
        int nozzle = f.nozzleTemp();
        if (nozzle) detail += (detail.length() ? "  " : "") + String(nozzle) + "\xC2\xB0" "C";
    } else if (last.printerTitle.length()) {
        // zmod_nfc decodes formats the reader does not know.
        title = last.printerTitle;
    } else {
        title = f.spoolId ? "Spool tag" : "Unknown tag";
        brand = tagKindName(last.tag.kind);
    }
    if (!title.length()) title = last.printerTitle;
    u8g2_->setFont(LARGE);
    String shown = title;
    while (shown.length() > 1 && u8g2_->getUTF8Width(shown.c_str()) > WIDTH) shown.remove(shown.length() - 1);
    u8g2_->drawUTF8(0, 25, shown.c_str());

    u8g2_->setFont(MEDIUM);
    int spool = last.printerSpool ? last.printerSpool : f.spoolId;
    int spoolWidth = 0;
    if (spool) {
        String id = "#" + String(spool);
        spoolWidth = u8g2_->getUTF8Width(id.c_str()) + 4;
        u8g2_->drawUTF8(WIDTH - spoolWidth + 4, 36, id.c_str());
    }
    while (brand.length() > 1 && u8g2_->getUTF8Width(brand.c_str()) > WIDTH - spoolWidth) brand.remove(brand.length() - 1);
    if (brand.length()) u8g2_->drawUTF8(0, 36, brand.c_str());
    if (detail.length()) u8g2_->drawUTF8(0, 46, detail.c_str());

    String status = last.status;
    if (last.waiting && last.printer >= 0 && last.printer < (int)app.config.printers.size())
        status = app.config.printers[last.printer].method == Method::ZmodNfc ? "Pick the slot on the printer's screen"
                                                                             : "Pick a slot on the web page";
    if (!last.ok && status.length()) status = "! " + status;
    wrap(status, detail.length() ? 47 : 39, detail.length() ? 2 : 3);
}
