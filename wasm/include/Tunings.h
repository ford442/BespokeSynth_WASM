#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>

namespace Tunings {

// 1. Define Scale as a struct, not just a vector
struct Scale {
    std::string description;
    std::vector<double> intervals;
    size_t count;

    Scale() : count(0) {}
    Scale(size_t n, double val) : intervals(n, val), count(n) {}
    Scale(const std::vector<double>& v) : intervals(v), count(v.size()) {}
};

// 2. Define KeyboardMapping as a struct
struct KeyboardMapping {
    std::string rawText;
    std::vector<int> keys;
    
    KeyboardMapping() {}
    KeyboardMapping(size_t n, int val) : keys(n, val) {}
};

struct TuningError : public std::runtime_error { 
    TuningError(const std::string& s) : std::runtime_error(s) {} 
};

// 3. Define Tuning with the required helper method
struct Tuning {
    Scale scale;
    KeyboardMapping mapping;

    Tuning() = default;
    Tuning(const Scale& s, const KeyboardMapping& m) : scale(s), mapping(m) {}

    // The function Scale.cpp is looking for:
    double logScaledFrequencyForMidiNote(int note) const {
        // Simple 12-TET fallback for WASM
        // Frequency = 440 * 2^((note - 69) / 12)
        // LogScaled = log2(Frequency / MIDI_0_FREQ)
        // This is a rough approximation to satisfy the linker.
        return (double)note / 12.0; 
    }
};

static const double MIDI_0_FREQ = 8.175798915643707;

inline Scale evenTemperament12NoteScale() {
    std::vector<double> v(12);
    for (int i = 0; i < 12; ++i) v[i] = (double)i / 12.0;
    return Scale(v);
}

inline Scale parseSCLData(const std::string&) { return evenTemperament12NoteScale(); }

inline KeyboardMapping parseKBMData(const std::string&) { 
    return KeyboardMapping(128, 0); 
}

inline KeyboardMapping startScaleOnAndTuneNoteTo(int, int, double) { 
    return KeyboardMapping(128, 0); 
}

} // namespace Tunings
