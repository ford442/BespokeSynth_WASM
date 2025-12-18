// Minimal Tunings stub for WASM builds (single consolidated definition)
#pragma once
#include <vector>
#include <stdexcept>
#include <string>
#include <cmath>

namespace Tunings {

struct Scale
{
    std::vector<double> values;
    int count = 0;
    std::string description;

    Scale() = default;
    Scale(int n, double v = 0.0) : values(n, v), count(n) {}

    double operator[](size_t i) const { return values[i]; }
    double& operator[](size_t i) { return values[i]; }
};

struct KeyboardMapping
{
    std::vector<int> mapping;
    std::string rawText;

    KeyboardMapping() = default;
    KeyboardMapping(int n, int val) : mapping(n, val) {}
};

inline constexpr double MIDI_0_FREQ = 8.175798915643707; // A reasonable default (C-1)

struct TuningError : public std::runtime_error { TuningError(const std::string& s): std::runtime_error(s){} };

struct Tuning
{
    Scale scale;
    KeyboardMapping mapping;

    Tuning() = default;
    Tuning(const Scale& s, const KeyboardMapping& m) : scale(s), mapping(m) {}

    double logScaledFrequencyForMidiNote(int note) const
    {
        // Best-effort: map MIDI note offset to frequency using 12-TET relative to MIDI 0
        return MIDI_0_FREQ * std::pow(2.0, note / 12.0);
    }
};

inline Scale evenTemperament12NoteScale()
{
    Scale s(12);
    for (int i = 0; i < 12; ++i) s.values[i] = double(i) / 12.0;
    s.count = 12;
    s.description = "12-TET";
    return s;
}

inline Scale parseSCLData(const std::string&)
{
    return evenTemperament12NoteScale();
}

inline KeyboardMapping parseKBMData(const std::string&)
{
    return KeyboardMapping(128, 0);
}

inline KeyboardMapping startScaleOnAndTuneNoteTo(int /*start*/, int /*refPitch*/, double /*refFreq*/)
{
    return KeyboardMapping(128, 0);
}

} // namespace Tunings
