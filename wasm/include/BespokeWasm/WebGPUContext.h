#pragma once

#include <webgpu/webgpu.h>
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

    // width/height are logical (CSS) pixels; the backing surface is configured
    // at width*devicePixelRatio x height*devicePixelRatio so rendering is sharp
    // on HiDPI displays while all layout/hit-testing math stays in CSS pixels.
    void resize(int width, int height);

    // Render Pass Management
    WGPURenderPassEncoder beginFrame();
    void endFrame(bool captureScreenshot = false);
    bool readCapturedPixels(std::vector<uint8_t>& outRgba, int& outWidth, int& outHeight);

    WGPURenderPassEncoder getCurrentPass() const { return mCurrentPass; }
    WGPUDevice getDevice() const { return mDevice; }
    WGPUQueue getQueue() const { return mQueue; }
    WGPUTextureFormat getSwapChainFormat() const { return mFormat; }
    WGPUInstance getInstance() const { return mInstance; }

    // True once the device-lost callback has fired; callers should stop
    // issuing GPU commands and surface a recoverable error instead of
    // continuing to draw into an invalid device.
    bool isDeviceLost() const { return mDeviceLost; }

    Uniforms mCurrentState;

    // Exposed so the WasmBridge callbacks (defined in the .cpp) can update
    // context state without befriending free functions.
    void notifyDeviceLost(const char* reason);

private:
    // Called when the device request completes successfully
    void onDeviceReady();

    // Release helpers
    void releaseGpuResources();

    WGPUInstance mInstance = nullptr;
    WGPUAdapter mAdapter = nullptr;
    WGPUDevice mDevice = nullptr;
    WGPUQueue mQueue = nullptr;
    WGPUSurface mSurface = nullptr;
    WGPUTextureFormat mFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureUsage mSurfaceUsages = WGPUTextureUsage_None;
    bool mSupportsCopySrc = false;
    bool mDeviceLost = false;
    // Set when wgpuSurfaceGetCurrentTexture() reports SuccessSuboptimal (canvas
    // size/format no longer matches the configured surface); reconfigure at the
    // start of the next frame rather than mid-frame, per the WebGPU spec.
    bool mNeedsReconfigure = false;
    std::function<void(bool)> mOnComplete;

    // Current frame state
    WGPUCommandEncoder mCurrentEncoder = nullptr;
    WGPUTexture mCurrentSurfaceTexture = nullptr;
    WGPUTextureView mCurrentView = nullptr;
    WGPURenderPassEncoder mCurrentPass = nullptr;

    // Physical (device) pixel dimensions of the configured surface — this is
    // width/height passed to resize() multiplied by devicePixelRatio.
    int mWidth = 0;
    int mHeight = 0;

    WGPUBuffer mScreenshotStagingBuffer = nullptr;
    size_t mScreenshotStagingBytes = 0;
    std::vector<uint8_t> mCapturedPixels;
    int mCapturedWidth = 0;
    int mCapturedHeight = 0;
    bool mCaptureReady = false;
};
