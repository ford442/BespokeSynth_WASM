#include "WebGPUContext.h"
#include <iostream>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>

// --- Callback Wrappers ---
// The Dawn/Emscripten webgpu shim expects callbacks with two user-data pointers.
// Some header versions don't provide a userdata field in the callback-info struct,
// so we use short-lived static pointers to pass state into the callback safely.
static WGPUAdapter* s_adapterPtr = nullptr;
static WGPUDevice*  s_devicePtr = nullptr;

void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void * userdata, void * userdata2) {
    if (status == WGPURequestAdapterStatus_Success) {
        if (s_adapterPtr) *s_adapterPtr = adapter;
    } else {
        std::cerr << "WebGPU Adapter Error: ";
        if (message.data) std::cerr.write(message.data, message.length);
        std::cerr << std::endl;
    }
}

void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void * userdata, void * userdata2) {
    if (status == WGPURequestDeviceStatus_Success) {
        if (s_devicePtr) *s_devicePtr = device;
    } else {
        std::cerr << "WebGPU Device Error: ";
        if (message.data) std::cerr.write(message.data, message.length);
        std::cerr << std::endl;
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

bool WebGPUContext::initialize(const char* selector) {
    // 1. Instance
    WGPUInstanceDescriptor instanceDesc = {};
    mInstance = wgpuCreateInstance(&instanceDesc);
    if (!mInstance) return false;

    // 2. Surface
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = {};
    canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = WGPUStringView{selector, strlen(selector)};

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (WGPUChainedStruct*)&canvasSource;
    mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);

    // 3. Adapter
    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;

    WGPURequestAdapterCallbackInfo adapterCb = {};
    adapterCb.callback = onAdapterRequest;
    adapterCb.userdata = &adapter; // header uses 'userdata' (lowercase)

    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, adapterCb);

    if (!adapter) {
        std::cerr << "Failed to obtain WebGPU Adapter" << std::endl;
        return false;
    }

    // 4. Device
    WGPUDeviceDescriptor deviceDesc = {};
    WGPURequestDeviceCallbackInfo deviceCb = {};
    deviceCb.callback = onDeviceRequest;
    deviceCb.userdata = &mDevice; // header uses 'userdata' (lowercase)

    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCb);

    if (!mDevice) {
        std::cerr << "Failed to obtain WebGPU Device" << std::endl;
        return false;
    }

    mQueue = wgpuDeviceGetQueue(mDevice);

    double w, h;
    emscripten_get_element_css_size(selector, &w, &h);
    resize((int)w, (int)h);

    return true;
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
