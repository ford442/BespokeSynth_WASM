#pragma once

namespace bespoke::wasm
{
   void processWasmAudio(const float* const* input, float* const* output,
                         int numInputChannels, int numOutputChannels, int numSamples);
   void processWasmAudioInterleaved(float* output, int frames);
   float wasmAudioCpuLoad();
}
