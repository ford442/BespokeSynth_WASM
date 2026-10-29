// Compatibility wrapper: the WASM build uses the vendored surge-synthesizer
// tuning-library (libs/tuning-library, same as desktop) instead of a stub.
// Keep `#include "Tunings.h"` / `#include "BespokeWasm/Tunings.h"` working for
// Source/Scale.cpp and WASM code alike.
//
// The library reports malformed .scl/.kbm input by throwing Tunings::TuningError.
// Only TuningTable.cpp (compiled with -fwasm-exceptions) may call the parsers;
// everything else should go through BespokeWasm/TuningTable.h.
#pragma once

// Third-party (libs/tuning-library): not held to this project's warning flags.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#include "../../../libs/tuning-library/include/Tunings.h"
#pragma clang diagnostic pop
