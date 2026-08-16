#include "BespokeWasm/AudioRtGuard.h"

namespace bespoke::wasm
{
   std::atomic<bool> gAudioCallbackActive{ false };
}
