// The sound side of the voice commands, without the hardware: MFCC
// features of a recording (fixed point: the ESP32-C6 has no FPU) and their
// distance by dynamic time warping. Also builds on a PC (VOICE_HOST), to
// tune it against recordings.
#pragma once

#include <stdint.h>

namespace voice {
namespace dsp {

constexpr int RATE = 16000;
constexpr int MAX_SAMPLES = RATE * 5 / 2;           // 2.5 s
// Features: 25 ms frames every 10 ms, 26 mel bands, 12 cepstral coefficients.
constexpr int FRAME = 400, HOP = 160, NFFT = 512, BINS = NFFT / 2 + 1;
constexpr int BANDS = 26, CEPS = 12;
constexpr int MAX_FRAMES = (MAX_SAMPLES - FRAME) / HOP + 1;
constexpr int MAX_TEMPLATE = 200;                   // frames kept of a take

extern int16_t features[MAX_FRAMES][CEPS];

void makeTables();
// Features of a recording; its speech ends up in features[0..n), n returned
// (0: no speech found). peak: the loudest sample.
int extract(const int16_t *x, int count, int &peak);
// Distance of two feature sequences (INT_MAX: too different in length).
int dtw(const int16_t (*a)[CEPS], int n, const int16_t (*b)[CEPS], int m);

}  // namespace dsp
}  // namespace voice
