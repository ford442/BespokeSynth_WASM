/**
 * BespokeSynth WASM - WebGPU Context
 * Provides WebGPU initialization and context management for browser-based rendering
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <webgpu/webgpu.h>
#include <emscripten/html5_webgpu.h>
#include <functional>
#include <string>
#include <vector>

namespace bespoke {
namespace wasm {

/**
 * WebGPU device and context wrapper
 * Manages the WebGPU instance, adapter, device, and swap chain
 */
class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

    // Initialize WebGPU - must be called before any rendering
    bool initialize(const char* canvasSelector = "#canvas");
    
    // Check if WebGPU is supported and initialized
    bool isInitialized() const { return mInitialized; }
    
    // Get the current render pass encoder for drawing
    WGPURenderPassEncoder beginFrame();
    
    // End the current frame and present
    void endFrame();
    
    // Resize the swap chain when canvas size changes
    void resize(int width, int height);
    
    // Getters for WebGPU handles
    WGPUDevice getDevice() const { return mDevice; }
    WGPUQueue getQueue() const { return mQueue; }
    WGPUSurface getSurface() const { return mSurface; }
    WGPUTextureFormat getSwapChainFormat() const { return mSwapChainFormat; }
    
    // Canvas dimensions
    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }
    
    // Error handling callback
    using ErrorCallback = std::function<void(const std::string&)>;
    void setErrorCallback(ErrorCallback callback) { mErrorCallback = callback; }

private:
    void createSwapChain();
    void handleDeviceLost();
    
    WGPUInstance mInstance = nullptr;
    WGPUAdapter mAdapter = nullptr;
    WGPUDevice mDevice = nullptr;
    WGPUQueue mQueue = nullptr;
    WGPUSurface mSurface = nullptr;
    WGPUSwapChain mSwapChain = nullptr;
    WGPUTextureFormat mSwapChainFormat = WGPUTextureFormat_BGRA8Unorm;
    
    WGPUTextureView mCurrentTextureView = nullptr;
    WGPUCommandEncoder mCommandEncoder = nullptr;
    WGPURenderPassEncoder mRenderPassEncoder = nullptr;
    
    int mWidth = 800;
    int mHeight = 600;
    bool mInitialized = false;
    
    ErrorCallback mErrorCallback;
};

} // namespace wasm
} // namespace bespoke
