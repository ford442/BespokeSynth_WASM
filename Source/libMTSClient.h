#pragma once
#include <cmath>

// Forward declare the class exactly as Scale.h does
class MTSClient;

// Use 'MTSClient*' in all functions to match the variable types in Scale.cpp

inline MTSClient* MTS_RegisterClient() { return nullptr; }

inline void MTS_DeregisterClient(MTSClient*) {}

inline bool MTS_HasMaster(MTSClient*) { return false; }

inline double MTS_NoteToFrequency(MTSClient*, int note, int) { 
    // Standard A440 tuning formula: f = 440 * 2^((note-69)/12)
    return 440.0 * std::pow(2.0, (note - 69) / 12.0); 
}

inline bool MTS_ShouldFilterNote(MTSClient*, int, int) { return false; }
