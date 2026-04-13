#include "WebGPUContext.h"
#include <iostream>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>

// --- WebGPU Callbacks ---
// Using the new-style callback API with WGPUCallbackInfo structs

// Forward declarations
class WebGPUContext;
static void handleAdapterRequest(WebGPUContext* context, WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message);
static void handleDeviceRequest(WebGPUContext* context, WGPURequestDeviceStatus status, WGPUDevice device, const char* message);

// Adapter callback - new style with WGPUStringView
static void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
    std::string msg;
    if (message.data && message.length > 0) {
        msg = std::string(message.data, message.length);
    }
    printf("WebGPUContext: onAdapterRequest called, status=%d\n", (int)status);
    handleAdapterRequest(static_cast<WebGPUContext*>(userdata1), status, adapter, msg.c_str());
}

// Device callback - new style with WGPUStringView
static void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
    std::string msg;
    if (message.data && message.length > 0) {
        msg = std::string(message.data, message.length);
    }
    printf("WebGPUContext: onDeviceRequest called, status=%d\n", (int)status);
    handleDeviceRequest(static_cast<WebGPUContext*>(userdata1), status, device, msg.c_str());
}

// Device error callback - new style (includes device pointer as first param)
static void deviceErrorCallback(WGPUDevice const * device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
    (void)device; // Unused
    std::string msg;
    if (message.data && message.length > 0) {
        msg = std::string(message.data, message.length);
    }
    printf("WebGPU Device Error (type=%d): %s\n", (int)type, msg.c_str());
}

// --- Implementation ---

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

        if (context && adapter) {
            printf("WebGPUContext: Adapter found, requesting device\n");
            WGPUDeviceDescriptor deviceDesc = {};
            
            // Set up error callback in device descriptor
            deviceDesc.uncapturedErrorCallbackInfo.callback = deviceErrorCallback;
            deviceDesc.uncapturedErrorCallbackInfo.userdata1 = context;
            deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

            // Use new callback info structure for device request
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
    mCurrentState.transform[0] = 1.0f; mCurrentState.transform[2] = 0.0f; mCurrentState.transform[4] = 0.0f;
    mCurrentState.transform[1] = 0.0f; mCurrentState.transform[3] = 1.0f; mCurrentState.transform[5] = 0.0f;
    mCurrentState.color[0] = 1.0f; mCurrentState.color[1] = 1.0f; mCurrentState.color[2] = 1.0f; mCurrentState.color[3] = 1.0f;
}

WebGPUContext::~WebGPUContext() {}

bool WebGPUContext::initializeAsync(const char* selector, std::function<void(bool)> onComplete) {
    mOnComplete = onComplete;

    // 1. Instance
    WGPUInstanceDescriptor instanceDesc = {};
    mInstance = wgpuCreateInstance(&instanceDesc);
    if (!mInstance) {
        printf("WebGPUContext: ERROR - Failed to create instance\n");
        if (mOnComplete) mOnComplete(false);
        return false;
    }
    printf("WebGPUContext: Instance created\n");

    // 2. Surface - use the Emscripten-specific surface source
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
    printf("WebGPUContext: Surface created for selector: %s\n", selector);

    // 3. Adapter request (asynchronous)
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;

    printf("WebGPUContext: Requesting adapter...\n");

    // Use new callback info structure
    WGPURequestAdapterCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onAdapterRequest;
    callbackInfo.userdata1 = this;
    callbackInfo.userdata2 = nullptr;
    
    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, callbackInfo);

    printf("WebGPUContext: Adapter request submitted (async)\n");
    return true;
}

void WebGPUContext::processEvents() {
    // Process pending WebGPU events.
    // On Emscripten, WebGPU callbacks (adapter/device requests) fire via browser
    // Promise resolution in the JS event loop — they do NOT require emscripten_sleep().
    // Using emscripten_sleep(0) here causes Asyncify reentrancy crashes when JS
    // calls bespoke_process_events() from multiple concurrent setInterval timers.
    // wgpuInstanceProcessEvents() is a safe, synchronous, non-Asyncify call.
    if (mInstance) {
        wgpuInstanceProcessEvents(mInstance);
    }
}

void WebGPUContext::onDeviceReady() {
    printf("WebGPUContext: onDeviceReady\n");
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

    // Error callback was already set in device descriptor
    
    printf("WebGPUContext: Getting surface capabilities...\n");
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(mSurface, mAdapter, &caps);
    if (caps.formatCount > 0) {
        mFormat = caps.formats[0];
        printf("WebGPUContext: Selected preferred surface format: %d\n", (int)mFormat);
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

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

void WebGPUContext::assignAdapter(WGPUAdapter adapter) {
    printf("WebGPUContext: assignAdapter called\n");
    mAdapter = adapter;
}

void WebGPUContext::assignDevice(WGPUDevice device) {
    printf("WebGPUContext: assignDevice called\n");
    mDevice = device;
    onDeviceReady();
}

void WebGPUContext::notifyComplete(bool success) {
    printf("WebGPUContext: notifyComplete success=%d\n", success ? 1 : 0);
    if (mOnComplete) mOnComplete(success);
}

void WebGPUContext::resize(int width, int height) {
    mWidth = width;
    mHeight = height;
    if (!mDevice || !mSurface) return;

    WGPUSurfaceConfiguration config = {};
    config.device = mDevice;
    config.format = mFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    
    wgpuSurfaceConfigure(mSurface, &config);
}

WGPURenderPassEncoder WebGPUContext::beginFrame() {
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

    const bool textureOk = (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                             surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
    if (!textureOk || surfaceTexture.texture == nullptr) {
        printf("WebGPUContext: WARNING - Failed to get surface texture, status=%d\n", (int)surfaceTexture.status);
        return nullptr;
    }
    
    if (!surfaceTexture.texture) {
        printf("WebGPUContext: ERROR - Surface texture is null\n");
        return nullptr;
    }

    mCurrentSurfaceTexture = surfaceTexture.texture;

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
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    mCurrentPass = wgpuCommandEncoderBeginRenderPass(mCurrentEncoder, &passDesc);
    if (!mCurrentPass) {
        printf("WebGPUContext: ERROR - Failed to begin render pass\n");
    }
    
    return mCurrentPass;
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

    if (mCurrentSurfaceTexture) {
        wgpuTextureRelease(mCurrentSurfaceTexture);
        mCurrentSurfaceTexture = nullptr;
    }
}
