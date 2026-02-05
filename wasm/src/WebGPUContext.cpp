#include "WebGPUContext.h"
#include <iostream>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>

// --- Helper methods for unified logic ---

// Helper to handle adapter request response
static void handleAdapterRequest(WebGPUContext* context, WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message);

// Helper to handle device request response
static void handleDeviceRequest(WebGPUContext* context, WGPURequestDeviceStatus status, WGPUDevice device, const char* message);

// --- Callback Wrappers (5-argument callbacks) ---

// 5-argument adapter callback (newer emscripten/dawn headers with userdata2 and WGPUStringView)
static void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void * userdata, void * userdata2) {
    // Convert WGPUStringView to C string or std::string if needed, or pass nullptr if empty
    std::string msg;
    if (message.data && message.length > 0) {
        msg = std::string(message.data, message.length);
    }
    handleAdapterRequest(static_cast<WebGPUContext*>(userdata), status, adapter, msg.c_str());
}

// 5-argument device callback (newer emscripten/dawn headers with userdata2 and WGPUStringView)
static void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void * userdata, void * userdata2) {
    std::string msg;
    if (message.data && message.length > 0) {
        msg = std::string(message.data, message.length);
    }
    handleDeviceRequest(static_cast<WebGPUContext*>(userdata), status, device, msg.c_str());
}

// Uncaptured device error callback to catch shader compilation/validation messages at runtime
#ifdef WGPUDeviceSetUncapturedErrorCallback
static void deviceUncapturedErrorCallback(WGPUErrorType type, WGPUStringView message, void* userdata) {
    const std::string msg = (message.data ? std::string(message.data, message.length) : std::string());
    printf("WebGPU Device Error (type=%d): %s\n", (int)type, msg.c_str());
}
#endif

// --- Implementation of Logic ---

static void handleDeviceRequest(WebGPUContext* context, WGPURequestDeviceStatus status, WGPUDevice device, const char* message) {
    printf("WebGPUContext: handleDeviceRequest called, status=%d\n", (int)status);

    if (status == WGPURequestDeviceStatus_Success) {
        printf("WebGPUContext: Device acquired, assigning to context\n");
        if (context)
            context->assignDevice(device);
    } else {
        std::cerr << "WebGPU Device Error: ";
        if (message) std::cerr << message;
        std::cerr << std::endl;
        if (context) context->notifyComplete(false);
    }
}

static void handleAdapterRequest(WebGPUContext* context, WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message) {
    printf("WebGPUContext: handleAdapterRequest called, status=%d\n", (int)status);

    if (status == WGPURequestAdapterStatus_Success) {
        if (context) context->assignAdapter(adapter);

        // Immediately request a device once an adapter is available
        if (context && adapter) {
            printf("WebGPUContext: Adapter found, requesting device\n");
            WGPUDeviceDescriptor deviceDesc = {};

            // Use new callback info structure
            WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
            deviceCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
            deviceCallbackInfo.callback = onDeviceRequest;
            deviceCallbackInfo.userdata1 = context;
            deviceCallbackInfo.userdata2 = nullptr;
            
            wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallbackInfo);
        }
    } else {
        std::cerr << "WebGPU Adapter Error: ";
        if (message) std::cerr << message;
        std::cerr << std::endl;
        if (context) context->notifyComplete(false);
    }
}

// -------------------------

WebGPUContext::WebGPUContext() {
    // Identity Matrix
    mCurrentState.transform[0] = 1.0f; mCurrentState.transform[2] = 0.0f; mCurrentState.transform[4] = 0.0f;
    mCurrentState.transform[1] = 0.0f; mCurrentState.transform[3] = 1.0f; mCurrentState.transform[5] = 0.0f;
    // White Color
    mCurrentState.color[0] = 1.0f; mCurrentState.color[1] = 1.0f; mCurrentState.color[2] = 1.0f; mCurrentState.color[3] = 1.0f;
}

WebGPUContext::~WebGPUContext() {}

bool WebGPUContext::initializeAsync(const char* selector, std::function<void(bool)> onComplete) {
    mOnComplete = onComplete;

    // 1. Instance
    WGPUInstanceDescriptor instanceDesc = {};
    mInstance = wgpuCreateInstance(&instanceDesc);
    if (!mInstance) {
        if (mOnComplete) mOnComplete(false);
        return false;
    }

    // 2. Surface
    // Always use WGPUEmscriptenSurfaceSourceCanvasHTMLSelector as we are using the emdawnwebgpu header
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = {};
    canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = WGPUStringView{selector, strlen(selector)};

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (WGPUChainedStruct*)&canvasSource;
    mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);

    if (!mSurface) {
        std::cerr << "WebGPUContext: Failed to create surface for selector: ";
        if (selector) std::cerr << selector;
        std::cerr << std::endl;
        if (mOnComplete) mOnComplete(false);
        return false;
    }

    // 3. Adapter request (asynchronous)
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;

    printf("WebGPUContext: initializeAsync started with selector=%s\n", selector ? selector : "(null)");

    // Use new callback info structure
    WGPURequestAdapterCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onAdapterRequest;
    callbackInfo.userdata1 = this;
    callbackInfo.userdata2 = nullptr;
    
    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, callbackInfo);

#ifdef __EMSCRIPTEN__
    // In Emscripten builds, `wgpuInstanceProcessEvents` is unsupported and will
    // abort. The Emscripten WebGPU implementation drives callbacks via the
    // browser event loop / requestAnimationFrame (see html5.h), so do not call
    // it here.
    // If native builds need explicit processing, they will use the call below.
#else
    // Process pending WebGPU events to trigger callbacks (non-Emscripten builds)
    wgpuInstanceProcessEvents(mInstance);
#endif

    // Return 'true' to indicate the async request was started.
    return true;
}

void WebGPUContext::processEvents() {
#ifdef __EMSCRIPTEN__
    // No-op on Emscripten: the JS WebGPU library handles event processing via
    // the browser's requestAnimationFrame / microtask scheduling. Calling
    // wgpuInstanceProcessEvents() here on Emscripten will abort.
    (void)mInstance;
#else
    if (mInstance) {
        wgpuInstanceProcessEvents(mInstance);
    }
#endif
}

void WebGPUContext::onDeviceReady() {
    printf("WebGPUContext: onDeviceReady\n");
    // Device (mDevice) is already set by the callback
    if (!mDevice) {
        printf("WebGPUContext: ERROR - Device is null in onDeviceReady\n");
        if (mOnComplete) mOnComplete(false);
        return;
    }

    printf("WebGPUContext: Getting device queue...\n");
    mQueue = wgpuDeviceGetQueue(mDevice);
    if (!mQueue) {
        printf("WebGPUContext: ERROR - Failed to get device queue\n");
        if (mOnComplete) mOnComplete(false);
        return;
    }

    // Register uncaptured device error callback (if available) to catch shader compile/validation messages
#ifdef WGPUDeviceSetUncapturedErrorCallback
    wgpuDeviceSetUncapturedErrorCallback(mDevice, deviceUncapturedErrorCallback, this);
    printf("WebGPUContext: Registered uncaptured error callback\n");
#else
    printf("WebGPUContext: Warning - device uncaptured error callback unavailable in this header\n");
#endif

    // Get current canvas size and configure surface
    printf("WebGPUContext: Configuring surface...\n");
    double w, h;
    emscripten_get_element_css_size("#canvas", &w, &h);
    if (w <= 0 || h <= 0) {
        printf("WebGPUContext: WARNING - Invalid canvas size: %.0fx%.0f, using defaults\n", w, h);
        w = 800;
        h = 600;
    }
    resize((int)w, (int)h);

    printf("WebGPUContext: Device ready, initialization complete\n");
    if (mOnComplete) mOnComplete(true);
}

void WebGPUContext::assignAdapter(WGPUAdapter adapter)
{
    printf("WebGPUContext: assignAdapter called\n");
    mAdapter = adapter;
}

void WebGPUContext::assignDevice(WGPUDevice device)
{
    printf("WebGPUContext: assignDevice called\n");
    mDevice = device;
    onDeviceReady();
}

void WebGPUContext::notifyComplete(bool success)
{
    printf("WebGPUContext: notifyComplete success=%d\n", success ? 1 : 0);
    if (mOnComplete) mOnComplete(success);
}

void WebGPUContext::resize(int width, int height) {
    mWidth = width;
    mHeight = height;
    if (!mDevice || !mSurface) return;

#ifdef WGPUSurfaceConfiguration
    WGPUSurfaceConfiguration config = {};
    config.device = mDevice;
    config.format = mFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    
    wgpuSurfaceConfigure(mSurface, &config);
#else
    // Surface configuration types unavailable in this header; runtime may
    // require a different code path to configure the canvas surface.
    printf("WebGPUContext: Warning - surface configuration unavailable in headers; skipping configure\n");
#endif
}

WGPURenderPassEncoder WebGPUContext::beginFrame() {
#ifndef WGPUSurfaceTexture
    // Surface texture types not available in this header -> cannot begin frame
    printf("WebGPUContext: ERROR - WGPUSurfaceTexture not available in headers\n");
    return nullptr;
#else
    if (!mSurface) {
        printf("WebGPUContext: ERROR - Surface is null in beginFrame\n");
        return nullptr;
    }
    
    if (!mDevice) {
        printf("WebGPUContext: ERROR - Device is null in beginFrame\n");
        return nullptr;
    }

    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(mSurface, &surfaceTexture);

    // FIX: Check if texture is valid instead of relying on the Status enum
    if (surfaceTexture.status != 0 && surfaceTexture.texture == nullptr) {
        printf("WebGPUContext: WARNING - Failed to get surface texture, status=%d\n", surfaceTexture.status);
        return nullptr;
    }
    
    if (!surfaceTexture.texture) {
        printf("WebGPUContext: ERROR - Surface texture is null\n");
        return nullptr;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = mFormat;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    
    mCurrentView = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);
    if (!mCurrentView) {
        printf("WebGPUContext: ERROR - Failed to create texture view\n");
        return nullptr;
    }

    WGPUCommandEncoderDescriptor encoderDesc = {};
    mCurrentEncoder = wgpuDeviceCreateCommandEncoder(mDevice, &encoderDesc);
    if (!mCurrentEncoder) {
        printf("WebGPUContext: ERROR - Failed to create command encoder\n");
        if (mCurrentView) {
            wgpuTextureViewRelease(mCurrentView);
            mCurrentView = nullptr;
        }
        return nullptr;
    }

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = mCurrentView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};

#ifdef WGPU_DEPTH_SLICE_UNDEFINED
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#else
    colorAttachment.depthSlice = 0xFFFFFFFF;
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    mCurrentPass = wgpuCommandEncoderBeginRenderPass(mCurrentEncoder, &passDesc);
    if (!mCurrentPass) {
        printf("WebGPUContext: ERROR - Failed to begin render pass\n");
    }
    
    return mCurrentPass;
#endif
}

void WebGPUContext::endFrame() {
    if (mCurrentPass) {
        wgpuRenderPassEncoderEnd(mCurrentPass);
        wgpuRenderPassEncoderRelease(mCurrentPass);
        mCurrentPass = nullptr;
    }

    if (mCurrentEncoder) {
        WGPUCommandBufferDescriptor cmdBufDesc = {};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(mCurrentEncoder, &cmdBufDesc);
        wgpuQueueSubmit(mQueue, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);
        wgpuCommandEncoderRelease(mCurrentEncoder);
        mCurrentEncoder = nullptr;
    }

    if (mCurrentView) {
        wgpuTextureViewRelease(mCurrentView);
        mCurrentView = nullptr;
    }
}
