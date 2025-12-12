/**
 * BespokeSynth WASM - SDL2 Audio Backend Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "SDL2AudioBackend.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace bespoke {
namespace wasm {

// Audio conversion constants
static const float kInt16ToFloatScale = 1.0f / 32768.0f;
static const float kFloatToInt16Scale = 32767.0f;

SDL2AudioBackend::SDL2AudioBackend() {
}

SDL2AudioBackend::~SDL2AudioBackend() {
    shutdown();
}

bool SDL2AudioBackend::initialize(int sampleRate, int bufferSize, int numOutputChannels, int numInputChannels) {
    mSampleRate = sampleRate;
    mBufferSize = bufferSize;
    mNumOutputChannels = numOutputChannels;
    mNumInputChannels = numInputChannels;
    
    printf("SDL2AudioBackend: Initializing SDL audio...\n");
    printf("  Sample rate: %d\n", sampleRate);
    printf("  Buffer size: %d\n", bufferSize);
    printf("  Output channels: %d\n", numOutputChannels);
    printf("  Input channels: %d\n", numInputChannels);
    
    // Initialize SDL audio subsystem
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL2AudioBackend: Failed to initialize SDL audio: %s\n", SDL_GetError());
        return false;
    }
    
    // Set up output device
    SDL_AudioSpec desired = {};
    desired.freq = sampleRate;
    desired.format = AUDIO_F32;
    desired.channels = numOutputChannels;
    desired.samples = bufferSize;
    desired.callback = audioCallbackStatic;
    desired.userdata = this;
    
    mOutputDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &mObtainedSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    
    if (mOutputDevice == 0) {
        printf("SDL2AudioBackend: Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }
    
    printf("SDL2AudioBackend: Opened audio device successfully\n");
    printf("  Obtained sample rate: %d\n", mObtainedSpec.freq);
    printf("  Obtained buffer size: %d\n", mObtainedSpec.samples);
    printf("  Obtained channels: %d\n", mObtainedSpec.channels);
    
    // Update actual values
    mSampleRate = mObtainedSpec.freq;
    mBufferSize = mObtainedSpec.samples;
    mNumOutputChannels = mObtainedSpec.channels;
    
    // Allocate channel buffers
    mOutputChannelBuffers.resize(numOutputChannels);
    mOutputChannelPtrs.resize(numOutputChannels);
    for (int i = 0; i < numOutputChannels; i++) {
        mOutputChannelBuffers[i].resize(bufferSize);
        mOutputChannelPtrs[i] = mOutputChannelBuffers[i].data();
    }
    
    mInterleavedOutputBuffer.resize(bufferSize * numOutputChannels);
    
    if (numInputChannels > 0) {
        mInputChannelBuffers.resize(numInputChannels);
        mInputChannelPtrs.resize(numInputChannels);
        for (int i = 0; i < numInputChannels; i++) {
            mInputChannelBuffers[i].resize(bufferSize);
            mInputChannelPtrs[i] = mInputChannelBuffers[i].data();
        }
        mInterleavedInputBuffer.resize(bufferSize * numInputChannels);
    }
    
    return true;
}

void SDL2AudioBackend::shutdown() {
    stop();
    
    if (mOutputDevice != 0) {
        SDL_CloseAudioDevice(mOutputDevice);
        mOutputDevice = 0;
    }
    
    if (mInputDevice != 0) {
        SDL_CloseAudioDevice(mInputDevice);
        mInputDevice = 0;
    }
    
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    
    printf("SDL2AudioBackend: Shutdown complete\n");
}

bool SDL2AudioBackend::start() {
    if (mOutputDevice == 0) {
        printf("SDL2AudioBackend: Cannot start - no audio device\n");
        return false;
    }
    
    SDL_PauseAudioDevice(mOutputDevice, 0);  // 0 = unpause (start playback)
    mIsRunning = true;
    
    printf("SDL2AudioBackend: Audio started\n");
    return true;
}

void SDL2AudioBackend::stop() {
    if (mOutputDevice != 0) {
        SDL_PauseAudioDevice(mOutputDevice, 1);  // 1 = pause (stop playback)
    }
    mIsRunning = false;
    
    printf("SDL2AudioBackend: Audio stopped\n");
}

std::vector<std::string> SDL2AudioBackend::getOutputDevices() {
    std::vector<std::string> devices;
    
    int count = SDL_GetNumAudioDevices(0);  // 0 = output devices
    for (int i = 0; i < count; i++) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name) {
            devices.push_back(name);
        }
    }
    
    return devices;
}

std::vector<std::string> SDL2AudioBackend::getInputDevices() {
    std::vector<std::string> devices;
    
    int count = SDL_GetNumAudioDevices(1);  // 1 = input devices
    for (int i = 0; i < count; i++) {
        const char* name = SDL_GetAudioDeviceName(i, 1);
        if (name) {
            devices.push_back(name);
        }
    }
    
    return devices;
}

void SDL2AudioBackend::audioCallbackStatic(void* userdata, Uint8* stream, int len) {
    SDL2AudioBackend* backend = static_cast<SDL2AudioBackend*>(userdata);
    backend->audioCallbackInternal(stream, len);
}

void SDL2AudioBackend::audioCallbackInternal(Uint8* stream, int len) {
    // Calculate number of frames
    int numSamples = len / sizeof(float) / mNumOutputChannels;
    
    // Clear output channel buffers
    for (int ch = 0; ch < mNumOutputChannels; ch++) {
        std::memset(mOutputChannelBuffers[ch].data(), 0, numSamples * sizeof(float));
    }
    
    // Call user callback
    if (mCallback) {
        mCallback(
            mNumInputChannels > 0 ? mInputChannelPtrs.data() : nullptr,
            mOutputChannelPtrs.data(),
            mNumInputChannels,
            mNumOutputChannels,
            numSamples
        );
    }
    
    // Interleave output
    float* output = reinterpret_cast<float*>(stream);
    float maxLevel = 0.0f;
    
    for (int i = 0; i < numSamples; i++) {
        for (int ch = 0; ch < mNumOutputChannels; ch++) {
            float sample = mOutputChannelBuffers[ch][i];
            output[i * mNumOutputChannels + ch] = sample;
            maxLevel = std::max(maxLevel, std::abs(sample));
        }
    }
    
    mOutputLevel.store(maxLevel);
}

void SDL2AudioBackend::convertToFloat(const Sint16* input, float* output, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        output[i] = input[i] * kInt16ToFloatScale;
    }
}

void SDL2AudioBackend::convertFromFloat(const float* input, Sint16* output, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        float sample = input[i];
        sample = std::max(-1.0f, std::min(1.0f, sample));  // Clamp
        output[i] = static_cast<Sint16>(sample * kFloatToInt16Scale);
    }
}

} // namespace wasm
} // namespace bespoke
