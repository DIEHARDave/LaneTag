// Voice command matching on a PC, with the firmware's own code: builds the
// templates from train_<Command>.wav files and matches the rest.
//   g++ -O2 -DVOICE_HOST -I../../src eval.cpp ../../src/voice_dsp.cpp -o eval
//   ./eval <train dir: NNN_train_Lane1.wav ...> [test dir: same names, labels known]
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "voice_dsp.h"

using namespace voice::dsp;

struct Take {
    std::string name, label;
    std::vector<int16_t> f;     // frames * CEPS
    int n = 0;
};

static std::vector<int16_t> readWav(const std::string &path) {
    std::vector<int16_t> out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return out;
    fseek(f, 44, SEEK_SET);
    int16_t buf[4096];
    size_t got;
    while ((got = fread(buf, 2, 4096, f)) > 0) out.insert(out.end(), buf, buf + got);
    fclose(f);
    return out;
}

static Take load(const std::string &path, const std::string &name) {
    Take t;
    t.name = name;
    std::vector<int16_t> x = readWav(path);
    int peak;
    t.n = extract(x.data(), (int)std::min<size_t>(x.size(), MAX_SAMPLES), peak);
    t.f.assign(&features[0][0], &features[0][0] + t.n * CEPS);
    return t;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    makeTables();
    auto list = [](const std::string &dir) {
        std::vector<std::string> files;
        DIR *d = opendir(dir.c_str());
        for (dirent *e; d && (e = readdir(d));)
            if (strstr(e->d_name, ".wav")) files.push_back(e->d_name);
        if (d) closedir(d);
        std::sort(files.begin(), files.end());
        return files;
    };
    auto labelled = [&](const std::string &dir) {
        std::vector<Take> out;
        for (auto &name : list(dir)) {
            Take t = load(dir + "/" + name, name);
            const char *tr = strstr(name.c_str(), "_train_");
            if (tr) t.label = std::string(tr + 7, name.c_str() + name.size() - 4);
            out.push_back(t);
        }
        return out;
    };
    std::vector<Take> train = labelled(argv[1]), test;
    if (argc > 2) test = labelled(argv[2]);
    // Leave-one-out over the training takes, then the test files.
    int right = 0, total = 0;
    auto classify = [&](const Take &q, const Take *skip, std::string &best, int &d1, int &d2) {
        std::vector<std::pair<std::string, int>> per;
        for (auto &t : train) {
            if (&t == skip || !t.n || !q.n) continue;
            int d = dtw((const int16_t(*)[CEPS])q.f.data(), q.n, (const int16_t(*)[CEPS])t.f.data(), t.n);
            auto it = std::find_if(per.begin(), per.end(), [&](auto &p) { return p.first == t.label; });
            if (it == per.end()) per.push_back({t.label, d});
            else it->second = std::min(it->second, d);
        }
        std::sort(per.begin(), per.end(), [](auto &a, auto &b) { return a.second < b.second; });
        best = per.size() ? per[0].first : "";
        d1 = per.size() ? per[0].second : -1;
        d2 = per.size() > 1 ? per[1].second : -1;
    };
    for (auto &t : train) {
        std::string best;
        int d1, d2;
        classify(t, &t, best, d1, d2);
        bool ok = best == t.label;
        right += ok;
        total++;
        printf("%-28s %4d fr  -> %-16s %6d %6d %s\n", t.name.c_str(), t.n, best.c_str(), d1, d2, ok ? "" : "WRONG");
    }
    printf("leave-one-out: %d/%d\n", right, total);
    int tr = 0;
    for (auto &q : test) {
        std::string best;
        int d1, d2;
        classify(q, nullptr, best, d1, d2);
        tr += best == q.label;
        printf("%-28s %4d fr  -> %-16s %6d %6d %s\n", q.name.c_str(), q.n, best.c_str(), d1, d2, best == q.label ? "" : "WRONG");
    }
    if (test.size()) printf("test: %d/%zu\n", tr, test.size());
}
