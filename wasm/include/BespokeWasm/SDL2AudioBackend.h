/**
 * BespokeSynth WASM - SDL2 Audio Backend
 * Provides audio I/O for browser-based synthesizer using SDL2
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <SDL2/SDL.h>
#include <functional>
#include <vector>
#include <atomic>

namespace bespoke {
namespace wasm {

/**
 * Audio callback type - receives input buffer and fills output buffer
 * @param input Input audio buffer (may be null if no input)
 * @param output Output audio buffer to fill
 * @param numInputChannels Number of input channels
 * @param numOutputChannels Number of output channels
 * @param numSamples Number of samples per channel
 */
using AudioCallback = std::function<void(
    const float* const* input,
    float* const* output,
    int numInputChannels,
    int numOutputChannels,
    int numSamples
)>;

/**
 * SDL2-based audio backend for WASM
 * Handles audio initialization, callback management, and buffer conversion
 */
class SDL2AudioBackend {
public:
    SDL2AudioBackend();
    ~SDL2AudioBackend();
    
    // Initialize audio system
    bool initialize(int sampleRate = 44100, int bufferSize = 512, int numOutputChannels = 2, int numInputChannels = 0);
    
    // Shutdown audio system
    void shutdown();
    
    // Start/stop audio processing
    bool start();
    void stop();
    
    // Set the audio processing callback
    void setCallback(AudioCallback callback) { mCallback = callback; }
    
    // Audio state
    bool isRunning() const { return mIsRunning; }
    int getSampleRate() const { return mSampleRate; }
    int getBufferSize() const { return mBufferSize; }
    int getNumOutputChannels() const { return mNumOutputChannels; }
    int getNumInputChannels() const { return mNumInputChannels; }
    
    // Get audio device info
    std::vector<std::string> getOutputDevices();
    std::vector<std::string> getInputDevices();
    
    // Audio level monitoring
    float getOutputLevel() const { return mOutputLevel.load(); }
    float getInputLevel() const { return mInputLevel.load(); }

private:
    static void audioCallbackStatic(void* userdata, Uint8* stream, int len);
    void audioCallbackInternal(Uint8* stream, int len);
    void convertToFloat(const Sint16* input, float* output, int numSamples);
    void convertFromFloat(const float* input, Sint16* output, int numSamples);
    
    SDL_AudioDeviceID mOutputDevice = 0;
    SDL_AudioDeviceID mInputDevice = 0;
    SDL_AudioSpec mObtainedSpec;
    
    AudioCallback mCallback;
    
    int mSampleRate = 44100;
    int mBufferSize = 512;
    int mNumOutputChannels = 2;
    int mNumInputChannels = 0;
    
    std::atomic<bool> mIsRunning{false};
    std::atomic<float> mOutputLevel{0.0f};
    std::atomic<float> mInputLevel{0.0f};
    
    // Intermediate buffers for channel separation
    std::vector<float> mInterleavedOutputBuffer;
    std::vector<float*> mOutputChannelPtrs;
    std::vector<std::vector<float>> mOutputChannelBuffers;
    
    std::vector<float> mInterleavedInputBuffer;
    std::vector<const float*> mInputChannelPtrs;
    std::vector<std::vector<float>> mInputChannelBuffers;
};

} // namespace wasm
} // namespace bespoke
