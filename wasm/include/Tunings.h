#pragma once
#include <vector>
#include <stdexcept>
#include <string>
#include <cmath>

namespace Tunings {

// Using double for better frequency precision
using Scale = std::vector<double>;
using KeyboardMapping = std::vector<int>;

static const double MIDI_0_FREQ = 8.175798915643707;

struct TuningError : public std::runtime_error { 
    TuningError(const std::string& s) : std::runtime_error(s) {} 
};

struct Tuning {
    Scale scale;
    KeyboardMapping mapping;
    Tuning() = default;
    Tuning(const Scale& s, const KeyboardMapping& m) : scale(s), mapping(m) {}
};

// Returns a standard 12-TET scale (0/12, 1/12, ... 11/12)
inline Scale evenTemperament12NoteScale() {
    Scale s(12);
    for (int i = 0; i < 12; ++i) {
        s[i] = static_cast<double>(i) / 12.0;
    }
    return s;
}

// Minimal stubs for WASM
inline Scale parseSCLData(const std::string&) { 
    return evenTemperament12NoteScale(); 
}

inline KeyboardMapping parseKBMData(const std::string&) { 
    // Return a default identity mapping
    KeyboardMapping map(128);
    for(int i=0; i<128; ++i) map[i] = i;
    return map; 
}

inline KeyboardMapping startScaleOnAndTuneNoteTo(int /*start*/, int /*refPitch*/, double /*refFreq*/) { 
    // Return a default identity mapping
    KeyboardMapping map(128);
    for(int i=0; i<128; ++i) map[i] = i;
    return map; 
}

} // namespace Tunings
