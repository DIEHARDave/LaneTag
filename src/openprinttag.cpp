#include "openprinttag.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace {

// The spec's material_type list.
const char *const MATERIAL_TYPES[] = {
    "PLA", "PETG", "TPU", "ABS", "ASA", "PC", "PCTG", "PP", "PA6", "PA11",
    "PA12", "PA66", "CPE", "TPE", "HIPS", "PHA", "PET", "PEI", "PBT", "PVB",
    "PVA", "PEKK", "PEEK", "BVOH", "TPC", "PPS", "PPSU", "PVC", "PEBA",
    "PVDF", "PPA", "PCL", "PES", "PMMA", "POM", "PPE", "PS", "PSU", "TPI",
    "SBS", "OBC", "EVA", "PA612",
};

// Main section keys.
constexpr int KEY_MATERIAL_TYPE = 9;
constexpr int KEY_MATERIAL_NAME = 10;
constexpr int KEY_BRAND = 11;
constexpr int KEY_NOMINAL_WEIGHT = 16;
constexpr int KEY_ACTUAL_WEIGHT = 17;
constexpr int KEY_COLOR = 19;
constexpr int KEY_DIAMETER = 30;
constexpr int KEY_NOZZLE_MIN = 34;
constexpr int KEY_NOZZLE_MAX = 35;
constexpr int KEY_BED_MIN = 37;
constexpr int KEY_BED_MAX = 38;
constexpr int KEY_MATERIAL_ABBREVIATION = 52;
// Meta section: offsets of the main and aux sections.
constexpr int META_MAIN = 0;
constexpr int META_AUX = 2;
// Aux section: grams consumed.
constexpr int AUX_CONSUMED = 0;

constexpr int MAX_DEPTH = 8;

struct Value {
    enum Type { None, Int, Float, Bytes, Text, Other } type = None;
    int64_t i = 0;
    double f = 0;
    std::vector<uint8_t> bytes;     // byte and text strings

    bool number() const { return type == Int || type == Float; }
    double asDouble() const { return type == Int ? (double)i : f; }
    std::string text() const { return std::string(bytes.begin(), bytes.end()); }
};

typedef std::map<int64_t, Value> IntMap;

class Reader {
public:
    Reader(const uint8_t *data, size_t size, size_t pos) : d_(data), size_(size), pos_(pos) {}
    size_t pos() const { return pos_; }

    // A map with integer keys; other keys and nested containers are
    // skipped.
    bool map(IntMap &out) {
        if (pos_ >= size_ || (d_[pos_] >> 5) != 5) return false;
        uint8_t info = d_[pos_++] & 0x1F;
        uint64_t count = 0;
        bool indefinite = info == 31;
        if (!indefinite && !argument(info, count)) return false;
        for (uint64_t n = 0; indefinite || n < count; n++) {
            if (indefinite && pos_ < size_ && d_[pos_] == 0xFF) {
                pos_++;
                return true;
            }
            Value key, value;
            if (!item(key, 1) || !item(value, 1)) return false;
            if (key.type == Value::Int) out[key.i] = value;
        }
        return true;
    }

private:
    bool argument(uint8_t info, uint64_t &value) {
        if (info < 24) {
            value = info;
            return true;
        }
        if (info > 27) return false;
        size_t bytes = (size_t)1 << (info - 24);
        if (pos_ + bytes > size_) return false;
        value = 0;
        for (size_t i = 0; i < bytes; i++) value = (value << 8) | d_[pos_++];
        return true;
    }

    static double half(uint16_t bits) {
        double sign = (bits & 0x8000) ? -1 : 1;
        int exp = (bits >> 10) & 0x1F, frac = bits & 0x3FF;
        if (exp == 0) return sign * frac * std::ldexp(1.0, -24);
        if (exp == 31) return frac ? NAN : sign * INFINITY;
        return sign * (1 + frac / 1024.0) * std::ldexp(1.0, exp - 15);
    }

    // One data item; strings keep their bytes, containers are walked over.
    // A break (0xFF) is an error here: containers consume their own.
    bool item(Value &out, int depth) {
        if (depth > MAX_DEPTH || pos_ >= size_) return false;
        uint8_t initial = d_[pos_++];
        uint8_t major = initial >> 5, info = initial & 0x1F;
        out = Value();
        if (major == 7) {
            if (info == 31) return false;
            uint64_t raw;
            if (info == 25 || info == 26 || info == 27) {
                if (!argument(info, raw)) return false;
                out.type = Value::Float;
                if (info == 25) {
                    out.f = half((uint16_t)raw);
                } else if (info == 26) {
                    uint32_t bits = (uint32_t)raw;
                    float f;
                    memcpy(&f, &bits, 4);
                    out.f = f;
                } else {
                    memcpy(&out.f, &raw, 8);
                }
                return true;
            }
            out.type = Value::Other;            // false, true, null, simple
            return argument(info, raw);
        }
        if (info == 31) {                       // indefinite length
            if (major == 2 || major == 3) {
                out.type = major == 2 ? Value::Bytes : Value::Text;
                while (true) {
                    if (pos_ >= size_) return false;
                    if (d_[pos_] == 0xFF) {
                        pos_++;
                        return true;
                    }
                    Value chunk;
                    if (!item(chunk, depth + 1)) return false;
                    out.bytes.insert(out.bytes.end(), chunk.bytes.begin(), chunk.bytes.end());
                }
            }
            if (major == 4 || major == 5) {
                out.type = Value::Other;
                for (int n = 0;; n++) {
                    if (pos_ >= size_) return false;
                    if (d_[pos_] == 0xFF) {
                        pos_++;
                        return true;
                    }
                    Value skip;
                    if (!item(skip, depth + 1)) return false;
                    if (major == 5 && !item(skip, depth + 1)) return false;
                }
            }
            return false;
        }
        uint64_t arg;
        if (!argument(info, arg)) return false;
        switch (major) {
        case 0:
            out.type = Value::Int;
            out.i = (int64_t)arg;
            return true;
        case 1:
            out.type = Value::Int;
            out.i = -1 - (int64_t)arg;
            return true;
        case 2:
        case 3:
            if (arg > size_ - pos_) return false;
            out.type = major == 2 ? Value::Bytes : Value::Text;
            out.bytes.assign(d_ + pos_, d_ + pos_ + arg);
            pos_ += arg;
            return true;
        case 4:
        case 5: {
            out.type = Value::Other;
            uint64_t items = major == 5 ? arg * 2 : arg;
            for (uint64_t n = 0; n < items; n++) {
                Value skip;
                if (!item(skip, depth + 1)) return false;
            }
            return true;
        }
        default:                                // 6: a tag; its content counts
            return item(out, depth + 1);
        }
    }

    const uint8_t *d_;
    size_t size_;
    size_t pos_;
};

const Value *find(const IntMap &map, int key) {
    auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}

int intOf(const IntMap &map, int key) {
    const Value *v = find(map, key);
    return v && v->number() ? (int)v->asDouble() : 0;
}

std::string textOf(const IntMap &map, int key) {
    const Value *v = find(map, key);
    return v && v->type == Value::Text ? v->text() : std::string();
}

}  // namespace

bool parseOpenPrintTag(const uint8_t *payload, size_t len, OpenPrintTag &out) {
    IntMap meta, main, aux;
    Reader metaReader(payload, len, 0);
    if (!metaReader.map(meta)) return false;
    size_t mainAt = metaReader.pos();
    const Value *offset = find(meta, META_MAIN);
    if (offset && offset->type == Value::Int) mainAt = (size_t)offset->i;
    if (mainAt >= len) return false;
    Reader mainReader(payload, len, mainAt);
    if (!mainReader.map(main)) return false;
    offset = find(meta, META_AUX);
    if (offset && offset->type == Value::Int && (size_t)offset->i < len) {
        Reader auxReader(payload, len, (size_t)offset->i);
        if (!auxReader.map(aux)) aux.clear();
    }

    out = OpenPrintTag();
    out.material = textOf(main, KEY_MATERIAL_ABBREVIATION);
    const Value *type = find(main, KEY_MATERIAL_TYPE);
    if (out.material.empty() && type && type->type == Value::Int && type->i >= 0 &&
        type->i < (int64_t)(sizeof(MATERIAL_TYPES) / sizeof(MATERIAL_TYPES[0])))
        out.material = MATERIAL_TYPES[type->i];
    out.brand = textOf(main, KEY_BRAND);
    out.name = textOf(main, KEY_MATERIAL_NAME);
    const Value *color = find(main, KEY_COLOR);
    if (color && color->type == Value::Bytes && color->bytes.size() >= 3) {
        const auto &c = color->bytes;
        out.color = ((int32_t)c[0] << 16) | (c[1] << 8) | c[2];
        if (c.size() > 3) out.alpha = c[3];
    }
    const Value *weight = find(main, KEY_ACTUAL_WEIGHT);
    if (!weight || !weight->number() || weight->asDouble() == 0) weight = find(main, KEY_NOMINAL_WEIGHT);
    if (weight && weight->number() && weight->asDouble() > 0) {
        double grams = weight->asDouble();
        const Value *consumed = find(aux, AUX_CONSUMED);
        if (consumed && consumed->number()) grams = grams - consumed->asDouble() > 0 ? grams - consumed->asDouble() : 0;
        out.weight = (int)grams;
    }
    const Value *diameter = find(main, KEY_DIAMETER);
    if (diameter && diameter->number() && diameter->asDouble() > 0) out.diameter = (float)diameter->asDouble();
    out.nozzleMin = intOf(main, KEY_NOZZLE_MIN);
    out.nozzleMax = intOf(main, KEY_NOZZLE_MAX);
    out.bedTemp = intOf(main, KEY_BED_MAX);
    if (!out.bedTemp) out.bedTemp = intOf(main, KEY_BED_MIN);
    return true;
}
