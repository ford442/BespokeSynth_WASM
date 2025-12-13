#include "WebGPUContext.h"
#include <iostream>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>

// --- Callback Wrappers for New Dawn API ---
// These store the result into the userdata pointer
void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * userdata) {
    if (status == WGPURequestAdapterStatus_Success) {
        *(WGPUAdapter*)userdata = adapter;
    } else {
        std::cerr << "WebGPU Adapter Error: " << (message ? message : "Unknown") << std::endl;
    }
}

void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, char const * message, void * userdata) {
    if (status == WGPURequestDeviceStatus_Success) {
        *(WGPUDevice*)userdata = device;
    } else {
        std::cerr << "WebGPU Device Error: " << (message ? message : "Unknown") << std::endl;
    }
}
// ------------------------------------------

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
    WGPUSurfaceSourceCanvasHTMLSelector canvasSource = {};
    canvasSource.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = WGPUStringView{selector, strlen(selector)};

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (const WGPUChainedStruct*)&canvasSource;
    mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);

    // 3. Adapter (Async-style API, but usually resolves immediately in Emscripten if data allows)
    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;

    WGPURequestAdapterCallbackInfo adapterCb = {};
    adapterCb.callback = onAdapterRequest;
    adapterCb.userdata = &adapter;
    
    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, adapterCb);

    if (!adapter) {
        std::cerr << "Failed to obtain WebGPU Adapter (Synchronous request failed)" << std::endl;
        return false;
    }

    // 4. Device
    WGPUDeviceDescriptor deviceDesc = {};
    WGPURequestDeviceCallbackInfo deviceCb = {};
    deviceCb.callback = onDeviceRequest;
    deviceCb.userdata = &mDevice;

    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCb);

    if (!mDevice) {
        std::cerr << "Failed to obtain WebGPU Device" << std::endl;
        return false;
    }

    mQueue = wgpuDeviceGetQueue(mDevice);

    // Initial Resize
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

    // In the new API, we must check status differently or just assume Success for now
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
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
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0}; // Dark background

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
    
    // Surface present is implicit in Emscripten loop usually, 
    // or handled by wgpuSurfacePresent if explicit presentation is enabled.
}
