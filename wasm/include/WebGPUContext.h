#pragma once

#include <webgpu/webgpu.h>
#include <vector>

struct Uniforms {
    float transform[6]; // 2x3 Matrix for 2D transforms
    float color[4];
};

class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

    bool init(const char* selector);
    void resize(int width, int height);
    void present();

    WGPUDevice getDevice() const { return mDevice; }
    WGPUQueue getQueue() const { return mQueue; }
    WGPUTextureFormat getSwapChainFormat() const { return mFormat; }
    
    // Helper to get the current texture view for rendering
    WGPUTextureView getCurrentTextureView();

    // Uniform state management
    Uniforms mCurrentState;

private:
    WGPUInstance mInstance = nullptr;
    WGPUDevice mDevice = nullptr;
    WGPUQueue mQueue = nullptr;
    WGPUSurface mSurface = nullptr; // Replaces SwapChain
    WGPUTextureFormat mFormat = WGPUTextureFormat_BGRA8Unorm;

    int mWidth = 0;
    int mHeight = 0;
};
