// Voice commands on boards with a microphone (the Waveshare
// ESP32-C6-LCD-1.69): hold the talk button (PWR), say a command, let go.
// The board matches what it heard against recordings of the user's own
// voice (MFCC features, dynamic time warping), all on the chip: no server.
// Each command is trained with three takes, started from the web page.
#pragma once

#ifdef HAS_VOICE

#include <Arduino.h>

namespace voice {

enum Command : uint8_t { NextPrinter, PreviousPrinter, Lane1, Lane2, Lane3, Lane4, Ask, COMMAND_COUNT };
constexpr int TAKES = 3;            // takes per training round
constexpr int MAX_TAKES = 6;        // kept per command (two rounds)

const char *commandName(int command);

void begin();
void loop();

// Whether the microphone started, and why not.
bool ready();
const String &error();

// The next TAKES takes of the talk button train command: in place of its
// takes, or (more) added to them, over the oldest ones once MAX_TAKES are
// kept. Takes from another day or held differently make it surer.
void train(int command, bool more);
void cancelTraining();
int training();             // the command being trained, or -1
int trainingTake();         // takes done so far
int takes(int command);     // stored takes of a command
void clear(int command);    // -1: all
// A take from a recording (16 kHz mono 16-bit samples, put into
// recordBuffer()): stored as take of command. "" or why not.
int16_t *recordBuffer();
size_t recordCapacity();    // samples
String storeTake(int command, int take, size_t count);

// The last attempt, for the web page.
struct Result {
    uint32_t id = 0;
    String heard;           // the command, or "" when nothing matched
    String action;          // what was done
    int distance = 0, second = 0;   // best and second-best command distances
    int frames = 0, peak = 0;       // speech frames found, loudest sample
};
const Result &last();

// The last recording as a WAV file (16 kHz mono), for checking the mic.
size_t wavSize();
void wavHeader(uint8_t header[44]);
const int16_t *samples();
size_t sampleCount();

}  // namespace voice

#endif
