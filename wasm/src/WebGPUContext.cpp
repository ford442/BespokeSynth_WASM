#include "WebGPUContext.h"
#include <iostream>
#include <cstdio>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>

// --- Callback Wrappers ---

// Forward-declare the device callback so it's visible when used from the adapter callback
void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void * userdata, void * userdata2);

void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void * userdata, void * userdata2) {
    printf("WebGPUContext: onAdapterRequest called, status=%d\n", (int)status);
    WebGPUContext* context = static_cast<WebGPUContext*>(userdata);

    if (status == WGPURequestAdapterStatus_Success) {
        if (context) context->assignAdapter(adapter);

        // Immediately request a device once an adapter is available
        if (context && adapter) {
            printf("WebGPUContext: Adapter found, requesting device\n");
            WGPUDeviceDescriptor deviceDesc = {};
            WGPURequestDeviceCallbackInfo deviceCb = {};
            deviceCb.callback = onDeviceRequest;
            deviceCb.userdata1 = context;
            deviceCb.userdata2 = nullptr;
            deviceCb.mode = WGPUCallbackMode_AllowProcessEvents; // ensure valid mode
            wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCb);
        }
    } else {
        std::cerr << "WebGPU Adapter Error: ";
        if (message.data) std::cerr.write(message.data, message.length);
        std::cerr << std::endl;
        if (context) context->notifyComplete(false);
    }
}

void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void * userdata, void * userdata2) {
    printf("WebGPUContext: onDeviceRequest called, status=%d\n", (int)status);
    WebGPUContext* context = static_cast<WebGPUContext*>(userdata);

    if (status == WGPURequestDeviceStatus_Success) {
        printf("WebGPUContext: Device acquired, assigning to context\n");
        if (context)
            context->assignDevice(device);
    } else {
        std::cerr << "WebGPU Device Error: ";
        if (message.data) std::cerr.write(message.data, message.length);
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
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = {};
    canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = WGPUStringView{selector, strlen(selector)};

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (WGPUChainedStruct*)&canvasSource;
    mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);

    // 3. Adapter request (asynchronous)
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;

    WGPURequestAdapterCallbackInfo adapterCb = {};
    adapterCb.callback = onAdapterRequest;
    adapterCb.userdata1 = this;
    adapterCb.userdata2 = nullptr;
    adapterCb.mode = WGPUCallbackMode_AllowProcessEvents; // ensure a valid callback mode

    printf("WebGPUContext: initializeAsync started with selector=%s\n", selector ? selector : "(null)");
    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, adapterCb);

    // Return 'true' to indicate the async request was started.
    return true;
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

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    mCurrentPass = wgpuCommandEncoderBeginRenderPass(mCurrentEncoder, &passDesc);
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
}
