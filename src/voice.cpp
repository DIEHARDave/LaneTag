#ifdef HAS_VOICE

#include "voice.h"

#include <ESP_I2S.h>
#include <LittleFS.h>
#include <Wire.h>
#include <limits.h>
#include <math.h>

#include "app.h"
#include "es8311.h"
#include "voice_dsp.h"

namespace voice {

namespace {

using namespace dsp;

// The ESP32-C6-LCD-1.69: ES8311 codec on I2C 8/7 (with its own I2S on
// 19-23), the PWR button on 18 (low while pressed).
constexpr int I2S_MCLK = 19, I2S_BCLK = 20, I2S_DIN = 21, I2S_WS = 22, I2S_DOUT = 23;
constexpr int I2C_SDA = 8, I2C_SCL = 7;
constexpr int TALK_PIN = 18;

constexpr int MIN_SAMPLES = RATE / 4;               // shorter presses are ignored
constexpr int SKIP_SAMPLES = RATE / 20;             // the button's click
constexpr int MIN_SPEECH = 15;                      // frames (150 ms)
constexpr uint32_t SHOWN = 3000;                    // a result on the screen
// A match must beat the next command by this much (percent), and be no
// further than MAX_DISTANCE.
constexpr int MARGIN = 5;
constexpr int MAX_DISTANCE = INT_MAX / 2;    // not tuned yet
constexpr uint32_t DEBOUNCE = 30;

const char *const NAMES[COMMAND_COUNT] = {"Next printer", "Previous printer", "Lane 1", "Lane 2",
                                          "Lane 3",       "Lane 4",           "Ask"};

I2SClass i2s;
bool ready_ = false;
String error_;
int16_t *audio = nullptr;
size_t audioCount = 0;
Result last_;
int trainCommand = -1, trainTake = 0, trainFirst = 0;
int takeCount[COMMAND_COUNT];

int16_t other[MAX_TEMPLATE][CEPS];

void setStatus(const String &text, uint32_t shownFor) {
    app.voiceText = text;
    app.voiceUntil = shownFor ? millis() + shownFor : 0;
    app.voiceShown = true;
    app.display.refresh();
}

String takePath(int command, int take) { return "/voice/" + String(command) + "_" + String(take) + ".bin"; }

void countTakes() {
    for (int c = 0; c < COMMAND_COUNT; c++) {
        takeCount[c] = 0;
        for (int t = 0; t < MAX_TAKES; t++)
            if (LittleFS.exists(takePath(c, t))) takeCount[c]++;
    }
}

bool saveTake(int command, int take, int n) {
    LittleFS.mkdir("/voice");
    File f = LittleFS.open(takePath(command, take), "w");
    if (!f) return false;
    uint16_t frames = n;
    bool ok = f.write((uint8_t *)&frames, 2) == 2 &&
              f.write((uint8_t *)features, sizeof(features[0]) * n) == sizeof(features[0]) * n;
    f.close();
    return ok;
}

int loadTake(int command, int take) {
    File f = LittleFS.open(takePath(command, take), "r");
    if (!f) return 0;
    uint16_t frames = 0;
    if (f.read((uint8_t *)&frames, 2) != 2 || frames > MAX_TEMPLATE ||
        f.read((uint8_t *)other, sizeof(other[0]) * frames) != (int)(sizeof(other[0]) * frames))
        frames = 0;
    f.close();
    return frames;
}

// Do what was said; returns what happened, for the screen.
String act(int command) {
    Config &config = app.config;
    int n = config.printers.size();
    if (command == NextPrinter || command == PreviousPrinter) {
        if (n < 2) return n ? "Only one printer" : "No printer";
        config.active = (config.active + (command == NextPrinter ? 1 : n - 1)) % n;
        saveConfig(config);
        app.feedback.play(Signal::Read);
        return config.printers[config.active].name;
    }
    if (config.active < 0 || config.active >= n) return "No printer";
    if (command >= Lane1 && command <= Lane4) {
        int slot = command - Lane1 + 1;
        const ScanRecord &last = app.last;
        int target = last.printer >= 0 && last.printer < n ? last.printer : config.active;
        if (last.waiting) {
            const PrinterConfig &printer = config.printers[target];
            if (slot > printer.slotCount()) return printer.name + " has no " + slotName(printer, slot);
            app.last.status = "Sending to " + slotName(printer, slot) + "...";
            app.last.waiting = false;
            if (!app.assignLast(target, slot).ok) app.last.waiting = true;
            return app.last.status;
        }
        PrinterConfig &printer = config.printers[config.active];
        if (!printer.hasSlots()) return printer.name + " has no lanes";
        if (slot > printer.slotCount())
            return printer.name + " has " + String(printer.slotCount()) + (printer.slotCount() == 1 ? " lane" : " lanes");
        printer.slot = slot;
        saveConfig(config);
        app.feedback.count(slot);
        return "Scans go to " + slotName(printer, slot);
    }
    if (command == Ask) {
        PrinterConfig &printer = config.printers[config.active];
        if (!printer.screenAsk()) return "Ask is for FF_NFC printers";
        printer.slot = 0;
        saveConfig(config);
        app.feedback.count(0);
        return "Scans ask for the slot";
    }
    return "";
}

String trainPrompt() {
    return "Hold PWR, say \"" + String(NAMES[trainCommand]) + "\" (" + String(trainTake + 1) + "/" + String(TAKES) + ")";
}

void heard(int count) {
    last_ = Result{last_.id + 1};
    int n = extract(audio, count, last_.peak);
    last_.frames = n;
    if (n < MIN_SPEECH) {
        last_.action = last_.peak < 300 ? "No sound from the microphone" : "Didn't hear a command";
        if (trainCommand >= 0)
            setStatus(last_.action + ". " + trainPrompt(), 0);
        else
            setStatus(last_.action, SHOWN);
        return;
    }
    if (trainCommand >= 0) {
        if (!saveTake(trainCommand, trainFirst + trainTake, n)) {
            setStatus("Could not save the take", SHOWN);
            trainCommand = -1;
            return;
        }
        last_.heard = NAMES[trainCommand];
        last_.action = "Take " + String(trainTake + 1) + " saved";
        countTakes();
        if (++trainTake < TAKES) {
            setStatus(trainPrompt(), 0);
        } else {
            setStatus("Learned \"" + String(NAMES[trainCommand]) + "\"", SHOWN);
            trainCommand = -1;
        }
        return;
    }
    int best = -1, d1 = INT_MAX, d2 = INT_MAX;
    for (int c = 0; c < COMMAND_COUNT; c++) {
        int d = INT_MAX;
        for (int t = 0; t < MAX_TAKES; t++) {
            int m = loadTake(c, t);
            if (m) d = std::min(d, dtw(features, n, other, m));
        }
        if (d < d1) {
            d2 = d1;
            d1 = d;
            best = c;
        } else if (d < d2) {
            d2 = d;
        }
    }
    last_.distance = d1 == INT_MAX ? -1 : d1;
    last_.second = d2 == INT_MAX ? -1 : d2;
    log_i("voice: best %s %d, next %d, %d frames, peak %d", best >= 0 ? NAMES[best] : "-", d1, d2, n, last_.peak);
    if (best < 0) {
        last_.action = "Train the commands on the web page first";
        setStatus(last_.action, SHOWN);
        return;
    }
    if (d1 > MAX_DISTANCE || (d2 != INT_MAX && (int64_t)d1 * 100 > (int64_t)d2 * (100 - MARGIN))) {
        last_.action = "Not sure what was said";
        setStatus(last_.action, SHOWN);
        return;
    }
    last_.heard = NAMES[best];
    last_.action = act(best);
    setStatus(last_.heard + ": " + last_.action, SHOWN);
}

// While the talk button is held: record (the rest of the firmware waits).
void listen() {
    app.usedAt = millis();
    setStatus(trainCommand >= 0 ? "Listening... (" + String(NAMES[trainCommand]) + ")" : String("Listening..."), 0);
    // Old samples still in the DMA buffers (available() is always non-zero,
    // so a fixed amount: 64 ms).
    for (int i = 0; i < 4; i++) i2s.readBytes((char *)audio, 512 * 2);
    size_t count = 0;
    uint32_t releasedAt = 0;
    while (count < (size_t)MAX_SAMPLES) {
        size_t got = i2s.readBytes((char *)(audio + count), std::min<size_t>(512, MAX_SAMPLES - count) * 2) / 2;
        count += got;
        if (digitalRead(TALK_PIN) == HIGH) {
            if (!releasedAt) releasedAt = millis();
            if (millis() - releasedAt > DEBOUNCE) break;
        } else {
            releasedAt = 0;
        }
        if (!got) delay(1);
    }
    app.usedAt = millis();
    if ((int)count < MIN_SAMPLES + SKIP_SAMPLES) {
        audioCount = 0;
        setStatus(trainCommand >= 0 ? trainPrompt() : String("Hold PWR while you speak"), trainCommand >= 0 ? 0 : SHOWN);
        return;
    }
    // Drop the button's click.
    count -= SKIP_SAMPLES;
    memmove(audio, audio + SKIP_SAMPLES, count * 2);
    audioCount = count;
    setStatus("...", 0);
    heard(count);
}

}  // namespace

const char *commandName(int command) { return command >= 0 && command < COMMAND_COUNT ? NAMES[command] : ""; }
bool ready() { return ready_; }
const String &error() { return error_; }
int training() { return trainCommand; }
int trainingTake() { return trainTake; }
int takes(int command) { return command >= 0 && command < COMMAND_COUNT ? takeCount[command] : 0; }
const Result &last() { return last_; }
const int16_t *samples() { return audio; }
size_t sampleCount() { return audioCount; }
size_t wavSize() { return 44 + audioCount * 2; }

void wavHeader(uint8_t h[44]) {
    auto put32 = [&](int at, uint32_t v) { memcpy(h + at, &v, 4); };
    auto put16 = [&](int at, uint16_t v) { memcpy(h + at, &v, 2); };
    memcpy(h, "RIFF", 4);
    put32(4, 36 + audioCount * 2);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, 1);
    put32(24, RATE);
    put32(28, RATE * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(h + 36, "data", 4);
    put32(40, audioCount * 2);
}

void train(int command, bool more) {
    if (command < 0 || command >= COMMAND_COUNT) return;
    if (!more)
        for (int t = 0; t < MAX_TAKES; t++) LittleFS.remove(takePath(command, t));
    countTakes();
    trainFirst = more && takeCount[command] + TAKES <= MAX_TAKES ? takeCount[command] : 0;
    trainCommand = command;
    trainTake = 0;
    setStatus(trainPrompt(), 0);
}

void cancelTraining() {
    if (trainCommand < 0) return;
    trainCommand = -1;
    setStatus("Training stopped", SHOWN);
}

void clear(int command) {
    for (int c = 0; c < COMMAND_COUNT; c++)
        if (command < 0 || c == command)
            for (int t = 0; t < MAX_TAKES; t++) LittleFS.remove(takePath(c, t));
    countTakes();
}

int16_t *recordBuffer() { return audio; }
size_t recordCapacity() { return audio ? MAX_SAMPLES : 0; }

String storeTake(int command, int take, size_t count) {
    if (command < 0 || command >= COMMAND_COUNT || take < 0 || take >= MAX_TAKES) return "no such command or take";
    if (!audio) return "not enough memory";
    audioCount = count;
    int peak, n = extract(audio, count, peak);
    if (n < MIN_SPEECH) return "no speech found in the recording";
    if (!saveTake(command, take, n)) return "could not save the take";
    countTakes();
    return "";
}

void begin() {
    pinMode(TALK_PIN, INPUT_PULLUP);
    makeTables();
    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) error_ = "no file system for the voice commands";
    countTakes();
    audio = (int16_t *)malloc(MAX_SAMPLES * 2);
    if (!audio) {
        error_ = "not enough memory to record";
        return;
    }
    Wire.begin(I2C_SDA, I2C_SCL);
    es8311_handle_t codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    const es8311_clock_config_t clock = {false, false, true, RATE * 256, RATE};
    if (!codec || es8311_init(codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
        es8311_microphone_config(codec, false) != ESP_OK) {
        error_ = "the audio chip (ES8311) does not answer";
        return;
    }
    es8311_microphone_gain_set(codec, ES8311_MIC_GAIN_24DB);
    i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
    if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
        error_ = "the microphone (I2S) did not start";
        return;
    }
    i2s.setTimeout(50);
    ready_ = error_.isEmpty();
}

void loop() {
    // Ready for a press only once it was let go (it also powers the board on).
    static bool armed = false, down = false;
    static uint32_t changedAt = 0;
    if (app.voiceShown && app.voiceUntil && (int32_t)(millis() - app.voiceUntil) >= 0) {
        app.voiceShown = false;
        app.display.refresh();
    }
    if (!ready_) return;
    bool pressed = digitalRead(TALK_PIN) == LOW;
    if (!pressed) armed = true;
    if (pressed != down && millis() - changedAt > DEBOUNCE) {
        down = pressed;
        changedAt = millis();
        if (pressed && armed) {
            listen();
            down = digitalRead(TALK_PIN) == LOW;
        }
    }
}

}  // namespace voice

#endif
