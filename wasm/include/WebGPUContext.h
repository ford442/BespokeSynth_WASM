#pragma once

#include <webgpu.h>
#include <vector>

struct Uniforms {
    float transform[6];
    float color[4];
};

#include <functional>

class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

    // Start asynchronous initialization. The provided callback will be invoked
    // on completion with 'true' for success or 'false' for failure.
    bool initializeAsync(const char* selector, std::function<void(bool)> onComplete);
    bool isInitialized() const { return mDevice != nullptr; }

    // Process pending WebGPU events (for async callbacks)
    void processEvents();

    // Helpers used by the WebGPU C callbacks (keeps internals private)
    void assignAdapter(WGPUAdapter adapter);
    void assignDevice(WGPUDevice device);
    void notifyComplete(bool success);

    void resize(int width, int height);
    
    // Render Pass Management
    WGPURenderPassEncoder beginFrame();
    void endFrame();

    WGPUDevice getDevice() const { return mDevice; }
    WGPUQueue getQueue() const { return mQueue; }
    WGPUTextureFormat getSwapChainFormat() const { return mFormat; }
    WGPUInstance getInstance() const { return mInstance; }

    Uniforms mCurrentState;

private:
    // Called when the device request completes successfully
    void onDeviceReady();

    WGPUInstance mInstance = nullptr;
    WGPUAdapter mAdapter = nullptr;
    WGPUDevice mDevice = nullptr;
    WGPUQueue mQueue = nullptr;
    WGPUSurface mSurface = nullptr;
    WGPUTextureFormat mFormat = WGPUTextureFormat_BGRA8Unorm;
    std::function<void(bool)> mOnComplete;
    
    // Current frame state
    WGPUCommandEncoder mCurrentEncoder = nullptr;
    WGPUTextureView mCurrentView = nullptr;
    WGPURenderPassEncoder mCurrentPass = nullptr;

    int mWidth = 0;
    int mHeight = 0;
};
