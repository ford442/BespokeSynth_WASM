#pragma once

// DUMMY LIB FOR WASM BUILD
// We place this in Source/ so Scale.cpp finds it immediately.

// Define the opaque struct expected by the code
struct MTS_Client;

// return nullptr to indicate no client
inline MTS_Client* MTS_RegisterClient() { return nullptr; }

// do nothing
inline void MTS_DeregisterClient(MTS_Client*) {}

// always return false (no master connected)
inline bool MTS_HasMaster(MTS_Client*) { return false; }

// Return standard frequency calculation (12-TET) since we have no microtuning
inline double MTS_NoteToFrequency(MTS_Client*, int note, int) { 
    // Standard A440 tuning formula: f = 440 * 2^((note-69)/12)
    return 440.0 * __builtin_pow(2.0, (note - 69) / 12.0); 
}

// Do not filter any notes
inline bool MTS_ShouldFilterNote(MTS_Client*, int, int) { return false; }
