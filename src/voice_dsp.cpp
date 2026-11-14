#if defined(HAS_VOICE) || defined(VOICE_HOST)

#include "voice_dsp.h"

#include <algorithm>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace voice {
namespace dsp {

namespace {

// Settings of the speech finder and the distance; a PC build reads them
// from the environment, to tune them against recordings.
#ifdef VOICE_HOST
int tune(const char *name, int value) {
    const char *v = getenv(name);
    return v ? atoi(v) : value;
}
#define TUNE(name, value) const int name = tune(#name, value)
#else
#define TUNE(name, value) constexpr int name = value
#endif
TUNE(T_DROP, 8 * 256);      // speech: within this much (log2 Q8) of the loudest frame
TUNE(T_FLOOR, 3 * 256);     // and this much over the noise floor
TUNE(T_GAP, 40);            // frames of pause between words
TUNE(T_PAD, 3);             // frames kept before and after
TUNE(T_VARNORM, 0);         // 1: also scale each coefficient to the same spread
TUNE(T_DELTA, 0);           // weight of the coefficients' change (quarters)
TUNE(T_SKIP, 1);            // coefficients left out of the distance (from c1)
TUNE(T_L2, 0);              // 1: squared distance instead of L1

// Fixed-point tables (the C6 has no FPU).
int16_t window[FRAME];          // Hamming, Q15
int16_t twCos[NFFT / 2], twSin[NFFT / 2];
int8_t binPoint[BINS];          // mel point below the bin, -1: outside
uint8_t binRise[BINS];          // weight of the band rising from it, Q8
int16_t dct[CEPS][BANDS];       // Q10
int32_t re[NFFT], im[NFFT];
int32_t energy[MAX_FRAMES];

}  // namespace

int16_t features[MAX_FRAMES][CEPS];

void makeTables() {
    for (int i = 0; i < FRAME; i++) window[i] = (int16_t)(32767 * (0.54f - 0.46f * cosf(2 * M_PI * i / (FRAME - 1))));
    for (int i = 0; i < NFFT / 2; i++) {
        twCos[i] = (int16_t)(32767 * cosf(2 * M_PI * i / NFFT));
        twSin[i] = (int16_t)(-32767 * sinf(2 * M_PI * i / NFFT));
    }
    // Mel points from 100 Hz to 7600 Hz, as FFT bins.
    auto mel = [](float hz) { return 2595 * log10f(1 + hz / 700); };
    float lo = mel(100), hi = mel(7600), points[BANDS + 2];
    for (int p = 0; p < BANDS + 2; p++) {
        float hz = 700 * (powf(10, (lo + (hi - lo) * p / (BANDS + 1)) / 2595) - 1);
        points[p] = hz * NFFT / RATE;
    }
    for (int k = 0; k < BINS; k++) {
        binPoint[k] = -1;
        for (int p = 0; p < BANDS + 1; p++)
            if (k >= points[p] && k < points[p + 1]) {
                binPoint[k] = p;
                binRise[k] = (uint8_t)std::min(255.0f, 256 * (k - points[p]) / (points[p + 1] - points[p]));
                break;
            }
    }
    for (int i = 0; i < CEPS; i++)
        for (int j = 0; j < BANDS; j++) dct[i][j] = (int16_t)(1024 * cosf(M_PI * (i + 1) * (j + 0.5f) / BANDS));
}

// log2 in Q8, close enough for features.
static int32_t log2q8(uint64_t x) {
    if (!x) return 0;
    int e = 63 - __builtin_clzll(x);
    uint32_t frac = e >= 8 ? (uint32_t)(x >> (e - 8)) & 0xFF : (uint32_t)(x << (8 - e)) & 0xFF;
    return e * 256 + frac;
}

static void fft() {
    for (int i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= NFFT; len <<= 1) {
        int step = NFFT / len;
        for (int i = 0; i < NFFT; i += len)
            for (int k = 0; k < len / 2; k++) {
                int32_t c = twCos[k * step], s = twSin[k * step];
                int32_t &ar = re[i + k], &ai = im[i + k], &br = re[i + k + len / 2], &bi = im[i + k + len / 2];
                int32_t tr = (int32_t)(((int64_t)br * c - (int64_t)bi * s) >> 15);
                int32_t ti = (int32_t)(((int64_t)br * s + (int64_t)bi * c) >> 15);
                br = ar - tr;
                bi = ai - ti;
                ar += tr;
                ai += ti;
            }
    }
}

// Features of the recording; the speech part ends up in features[0..n).
int extract(const int16_t *x, int count, int &peak) {
    int64_t sum = 0;
    for (int i = 0; i < count; i++) sum += x[i];
    int32_t dc = (int32_t)(sum / std::max(count, 1));
    peak = 0;
    for (int i = 0; i < count; i++) peak = std::max(peak, (int)abs((int32_t)x[i] - dc));
    int frames = count < FRAME ? 0 : (count - FRAME) / HOP + 1;
    for (int f = 0; f < frames; f++) {
        const int16_t *s = x + f * HOP;
        int32_t prev = f ? s[-1] - dc : 0;
        uint64_t raw = 0;
        for (int i = 0; i < NFFT; i++) {
            if (i < FRAME) {
                int32_t v = s[i] - dc;
                raw += (int64_t)v * v;
                int32_t emph = v - ((prev * 31) >> 5);      // pre-emphasis 0.97
                prev = v;
                re[i] = (emph * window[i]) >> 15;
            } else {
                re[i] = 0;
            }
            im[i] = 0;
        }
        fft();
        uint64_t bands[BANDS] = {0}, total = 0;
        for (int k = 0; k < BINS; k++) {
            uint64_t p = (uint64_t)((int64_t)re[k] * re[k] + (int64_t)im[k] * im[k]);
            total += p >> 8;
            int point = binPoint[k];
            if (point < 0) continue;
            uint64_t q = p >> 8;
            if (point < BANDS) bands[point] += q * binRise[k];
            if (point > 0) bands[point - 1] += q * (256 - binRise[k]);
        }
        (void)total;
        energy[f] = log2q8(raw);    // before pre-emphasis: clicks stay quiet
        int32_t logs[BANDS];
        for (int j = 0; j < BANDS; j++) logs[j] = log2q8(bands[j] + 1);
        for (int i = 0; i < CEPS; i++) {
            int32_t c = 0;
            for (int j = 0; j < BANDS; j++) c += logs[j] * dct[i][j];
            features[f][i] = (int16_t)std::min(32767, std::max(-32768, (int)(c >> 10)));
        }
    }
    if (!frames) return 0;
    // The speech: frames within 24 dB of the loudest and 9 dB over the
    // noise floor (the quietest tenth), joined across pauses between words
    // of up to MAX_GAP frames; of those stretches the one with the most
    // sound (a click or breath elsewhere is left out).
    int32_t loudest = 0, sorted[MAX_FRAMES];
    for (int f = 0; f < frames; f++) {
        loudest = std::max(loudest, energy[f]);
        sorted[f] = energy[f];
    }
    std::sort(sorted, sorted + frames);
    int32_t floor = sorted[frames / 10];
    if (loudest - floor < 4 * 256) return 0;     // no speech over the noise
    int32_t threshold = std::max(loudest - T_DROP, floor + T_FLOOR);
    int first = -1, lastFrame = -1, segStart = -1, segEnd = -1, gap = 0;
    int64_t best = -1, score = 0;
    for (int f = 0; f <= frames; f++) {
        bool loud = f < frames && energy[f] >= threshold;
        if (loud && segStart >= 0 && gap <= T_GAP) {
            segEnd = f;
            score += energy[f] - threshold;
            gap = 0;
            continue;
        }
        if (loud || f == frames) {
            if (segStart >= 0 && score > best) {
                best = score;
                first = segStart;
                lastFrame = segEnd;
            }
            if (loud) {
                segStart = segEnd = f;
                score = energy[f] - threshold;
                gap = 0;
            }
            continue;
        }
        gap++;
    }
    if (first < 0) return 0;
    first = std::max(0, first - T_PAD);
    lastFrame = std::min(frames - 1, lastFrame + T_PAD);
    int n = std::min(lastFrame - first + 1, MAX_TEMPLATE);
    if (first) memmove(features, features[first], sizeof(features[0]) * n);
    // Cepstral mean normalization: the same words through another distance
    // or level come out alike.
    for (int i = 0; i < CEPS; i++) {
        int32_t mean = 0;
        for (int f = 0; f < n; f++) mean += features[f][i];
        mean /= n;
        for (int f = 0; f < n; f++) features[f][i] -= mean;
        if (T_VARNORM) {
            int64_t sq = 0;
            for (int f = 0; f < n; f++) sq += (int32_t)features[f][i] * features[f][i];
            int32_t sd = (int32_t)sqrt((double)(sq / n)) + 1;
            for (int f = 0; f < n; f++) features[f][i] = (int16_t)((int32_t)features[f][i] * 256 / sd);
        }
    }
    return n;
}

// Dynamic time warping, L1 frame distance, normalized by the path length.
int dtw(const int16_t (*a)[CEPS], int n, const int16_t (*b)[CEPS], int m) {
    if (n > 2 * m || m > 2 * n) return INT_MAX;
    if (n > MAX_TEMPLATE || m > MAX_TEMPLATE) return INT_MAX;
    static int32_t rows[2][MAX_TEMPLATE + 1];
    constexpr int32_t INF = INT32_MAX / 2;
    int band = std::max(abs(n - m), std::max(n, m) / 4) + 2;
    int32_t *prev = rows[0], *cur = rows[1];
    for (int j = 0; j <= m; j++) prev[j] = INF;
    prev[0] = 0;
    for (int i = 1; i <= n; i++) {
        cur[0] = INF;
        int center = i * m / n;
        for (int j = 1; j <= m; j++) {
            if (abs(j - center) > band) {
                cur[j] = INF;
                continue;
            }
            int32_t d = 0;
            for (int k = T_SKIP; k < CEPS; k++) {
                int32_t e = a[i - 1][k] - b[j - 1][k];
                d += T_L2 ? (e * e) >> 8 : abs(e);
            }
            if (T_DELTA) {
                // The change around each frame.
                int i0 = std::max(i - 2, 0), i1 = std::min(i, n - 1), j0 = std::max(j - 2, 0), j1 = std::min(j, m - 1);
                int32_t dd = 0;
                for (int k = T_SKIP; k < CEPS; k++)
                    dd += abs((a[i1][k] - a[i0][k]) - (b[j1][k] - b[j0][k]));
                d += dd * T_DELTA / 4;
            }
            int32_t best = std::min(prev[j - 1], std::min(prev[j], cur[j - 1]));
            cur[j] = best >= INF ? INF : best + d;
        }
        std::swap(prev, cur);
    }
    return prev[m] >= INF ? INT_MAX : prev[m] / (n + m);
}

}  // namespace dsp
}  // namespace voice

#endif
