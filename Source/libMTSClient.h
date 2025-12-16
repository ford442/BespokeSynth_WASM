#pragma once
#include <cmath>

// Define the opaque struct expected by the code
struct MTS_Client;

// KEY FIX: Define the alias so 'MTSClient' and 'MTS_Client' are treated as the same type
// This bridges the gap between different versions of the library usage.
typedef struct MTS_Client MTSClient; 

inline MTS_Client* MTS_RegisterClient() { return nullptr; }

inline void MTS_DeregisterClient(MTS_Client*) {}

inline bool MTS_HasMaster(MTS_Client*) { return false; }

inline double MTS_NoteToFrequency(MTS_Client*, int note, int) { 
    // Standard A440 tuning formula: f = 440 * 2^((note-69)/12)
    return 440.0 * std::pow(2.0, (note - 69) / 12.0); 
}

inline bool MTS_ShouldFilterNote(MTS_Client*, int, int) { return false; }
