// Minimal Tunings stub for WASM builds
#pragma once
#include <vector>
#include <stdexcept>

namespace Tunings {

using Scale = std::vector<float>;

struct KeyboardMapping { int dummy = 0; };

struct TuningError : public std::runtime_error { TuningError(const char* s): std::runtime_error(s) {} };

struct Tuning {
    Tuning() {}
    Tuning(const Scale&, const KeyboardMapping&) {}
};

static const float MIDI_0_FREQ = 8.1757989156f;

inline Scale evenTemperament12NoteScale() {
    Scale s(12);
    for (int i = 0; i < 12; ++i) s[i] = float(i) / 12.0f;
    return s;
}

inline Scale parseSCLData(const std::string&) { return evenTemperament12NoteScale(); }
inline KeyboardMapping parseKBMData(const std::string&) { return KeyboardMapping{}; }
inline KeyboardMapping startScaleOnAndTuneNoteTo(int, int, float) { return KeyboardMapping{}; }

} // namespace Tunings
#pragma once
#include <vector>
#include <stdexcept>

namespace Tunings {

using Scale = std::vector<double>;
using KeyboardMapping = std::vector<int>;

inline constexpr double MIDI_0_FREQ = 8.175798915643707; // A reasonable default (C-1)

struct TuningError : public std::runtime_error { TuningError(const std::string& s): std::runtime_error(s){} };

struct Tuning {
    Scale scale;
    KeyboardMapping mapping;
    Tuning() = default;
    Tuning(const Scale& s, const KeyboardMapping& m): scale(s), mapping(m) {}
};

inline Scale evenTemperament12NoteScale() { return Scale(12, 0.0); }
inline Scale parseSCLData(const std::string&) { return Scale(12, 0.0); }
inline KeyboardMapping startScaleOnAndTuneNoteTo(int /*start*/, int /*refPitch*/, double /*refFreq*/) { return KeyboardMapping(128, 0); }
inline KeyboardMapping parseKBMData(const std::string&) { return KeyboardMapping(128, 0); }

} // namespace Tunings
