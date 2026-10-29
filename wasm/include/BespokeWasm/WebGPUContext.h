#pragma once

#include <webgpu/webgpu.h>
#include <string>

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

    // Render Pass Management. Screenshots read the canvas itself (toDataURL in the
    // same task as the frame), so there is no GPU readback here.
    WGPURenderPassEncoder beginFrame();
    void endFrame();

    // Validation, Internal and OutOfMemory scopes around object creation (pipelines,
    // shader modules). popErrorScopes resolves asynchronously with ok=false and the
    // first error message if anything inside failed, so a bad WGSL edit fails init
    // instead of leaving a black canvas.
    struct PendingErrorScopes {
        int remaining = 0;
        bool failed = false;
        std::string message;
        std::function<void(bool, const std::string&)> onComplete;
    };
    void pushErrorScopes();
    void popErrorScopes(std::function<void(bool, const std::string&)> onComplete);

    WGPURenderPassEncoder getCurrentPass() const { return mCurrentPass; }
    WGPUDevice getDevice() const { return mDevice; }
    WGPUQueue getQueue() const { return mQueue; }
    WGPUTextureFormat getSwapChainFormat() const { return mFormat; }
    WGPUInstance getInstance() const { return mInstance; }

    // True once the device-lost callback has fired; callers should stop
    // issuing GPU commands and surface a recoverable error instead of
    // continuing to draw into an invalid device.
    bool isDeviceLost() const { return mDeviceLost; }
    // True while releaseGpuResources() runs; the device-lost callback it triggers is ignored.
    bool isTearingDown() const { return mTearingDown; }

    Uniforms mCurrentState;

    // Exposed so the WasmBridge callbacks (defined in the .cpp) can update
    // context state without befriending free functions.
    void notifyDeviceLost(const char* reason);

private:
    // Called when the device request completes successfully
    void onDeviceReady();

    // Release helpers
    void releaseGpuResources();
    // Configure the surface again at the canvas's current CSS size and devicePixelRatio.
    void reconfigureSurface();

    WGPUInstance mInstance = nullptr;
    WGPUAdapter mAdapter = nullptr;
    WGPUDevice mDevice = nullptr;
    WGPUQueue mQueue = nullptr;
    WGPUSurface mSurface = nullptr;
    WGPUTextureFormat mFormat = WGPUTextureFormat_BGRA8Unorm;
    bool mDeviceLost = false;
    bool mTearingDown = false;
    WGPUSurfaceGetCurrentTextureStatus mLastSurfaceStatus = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
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
    // Logical size last passed to resize(), the fallback when the canvas has no CSS size.
    int mLogicalWidth = 0;
    int mLogicalHeight = 0;
};
