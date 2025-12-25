#include "WebGPUContext.h"
#include <iostream>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>

// --- Callback Wrappers (compat shims for different webgpu headers) ---

// Adapter/device callback using the Emscripten-provided signature (const char* message)
static void onDeviceRequest_compat(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void * userdata) {
    printf("WebGPUContext: onDeviceRequest called, status=%d\n", (int)status);
    WebGPUContext* context = static_cast<WebGPUContext*>(userdata);

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

static void onAdapterRequest_compat(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void * userdata) {
    printf("WebGPUContext: onAdapterRequest called, status=%d\n", (int)status);
    WebGPUContext* context = static_cast<WebGPUContext*>(userdata);

    if (status == WGPURequestAdapterStatus_Success) {
        if (context) context->assignAdapter(adapter);

        // Immediately request a device once an adapter is available
        if (context && adapter) {
            printf("WebGPUContext: Adapter found, requesting device\n");
            WGPUDeviceDescriptor deviceDesc = {};
            // Use the simple callback-based API available in older headers
            wgpuAdapterRequestDevice(adapter, &deviceDesc, onDeviceRequest_compat, context);
        }
    } else {
        std::cerr << "WebGPU Adapter Error: ";
        if (message) std::cerr << message;
        std::cerr << std::endl;
        if (context) context->notifyComplete(false);
    }
}

// If the newer 'info' style callbacks are available in headers, keep wrappers to them
#ifdef WGPUStringView
// Forward-declare the device callback so it's visible when used from the adapter callback
void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void * userdata, void * userdata2);

void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void * userdata, void * userdata2) {
    // Adapt to our compat handler by copying the string
    const std::string msg = (message.data ? std::string(message.data, message.length) : std::string());
    onAdapterRequest_compat(status, adapter, msg.c_str(), userdata);
}

void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void * userdata, void * userdata2) {
    const std::string msg = (message.data ? std::string(message.data, message.length) : std::string());
    onDeviceRequest_compat(status, device, msg.c_str(), userdata);
}
#endif
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
#ifdef WGPUEmscriptenSurfaceSourceCanvasHTMLSelector
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
#else
    // Emscripten headers do not expose the canvas selector chained struct on this
    // build; fall back to leaving the surface unset for now. At runtime, JS may
    // need to create/attach the surface or we can add a compatibility path later.
    mSurface = nullptr;
    printf("WebGPUContext: Warning - canvas selector surface not available in headers; mSurface left null\n");
#endif

    // 3. Adapter request (asynchronous)
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;

    printf("WebGPUContext: initializeAsync started with selector=%s\n", selector ? selector : "(null)");

    // Use the callback-based API available in current emscripten headers.
    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, onAdapterRequest_compat, this);

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
        if (mOnComplete) mOnComplete(false);
        return;
    }

    mQueue = wgpuDeviceGetQueue(mDevice);

    // Get current canvas size and configure surface
    double w, h;
    emscripten_get_element_css_size("#canvas", &w, &h);
    resize((int)w, (int)h);

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
    return nullptr;
#else
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(mSurface, &surfaceTexture);

    // FIX: Check if texture is valid instead of relying on the Status enum
    if (surfaceTexture.status != 0 && surfaceTexture.texture == nullptr) {
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

    WGPUCommandEncoderDescriptor encoderDesc = {};
    mCurrentEncoder = wgpuDeviceCreateCommandEncoder(mDevice, &encoderDesc);

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
