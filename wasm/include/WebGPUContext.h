#pragma once

#include <webgpu/webgpu.h>
#include <vector>

struct Uniforms {
    float transform[6];
    float color[4];
};

class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

    // Renamed from 'init' to match existing code usage
    bool initialize(const char* selector);
    bool isInitialized() const { return mDevice != nullptr; }

    void resize(int width, int height);
    
    // Render Pass Management
    WGPURenderPassEncoder beginFrame();
    void endFrame();

    WGPUDevice getDevice() const { return mDevice; }
    WGPUQueue getQueue() const { return mQueue; }
    WGPUTextureFormat getSwapChainFormat() const { return mFormat; }

    Uniforms mCurrentState;

private:
    WGPUInstance mInstance = nullptr;
    WGPUDevice mDevice = nullptr;
    WGPUQueue mQueue = nullptr;
    WGPUSurface mSurface = nullptr;
    WGPUTextureFormat mFormat = WGPUTextureFormat_BGRA8Unorm;
    
    // Current frame state
    WGPUCommandEncoder mCurrentEncoder = nullptr;
    WGPUTextureView mCurrentView = nullptr;
    WGPURenderPassEncoder mCurrentPass = nullptr;

    int mWidth = 0;
    int mHeight = 0;
};
