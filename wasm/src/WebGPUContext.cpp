#include "WebGPUContext.h"
#include <iostream>
#include <emscripten/html5.h>
#include <cstring> // for strlen

WebGPUContext::WebGPUContext() {
    // Initialize default identity matrix
    mCurrentState.transform[0] = 1.0f; mCurrentState.transform[2] = 0.0f; mCurrentState.transform[4] = 0.0f;
    mCurrentState.transform[1] = 0.0f; mCurrentState.transform[3] = 1.0f; mCurrentState.transform[5] = 0.0f;
    // Default white color
    mCurrentState.color[0] = 1.0f; mCurrentState.color[1] = 1.0f; mCurrentState.color[2] = 1.0f; mCurrentState.color[3] = 1.0f;
}

WebGPUContext::~WebGPUContext() {
    // Cleanup handled by WebGPU internal ref counting mostly, 
    // but explicit release is good practice in C++
}

bool WebGPUContext::init(const char* selector) {
    // 1. Create Instance
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = nullptr;
    mInstance = wgpuCreateInstance(&instanceDesc);
    if (!mInstance) {
        std::cerr << "Could not initialize WebGPU Instance" << std::endl;
        return false;
    }

    // 2. Create Surface from Canvas
    WGPUSurfaceSourceCanvasHTMLSelector canvasSource = {};
    canvasSource.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = WGPUStringView{selector, strlen(selector)};

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (const WGPUChainedStruct*)&canvasSource;
    mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);

    // 3. Request Adapter (Synchronous for simplicity in this port, usually async)
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = mSurface;
    
    // NOTE: In Emscripten/Dawn C++, we often need to use a helper or callback.
    // However, emdawnwebgpu allows wgpuInstanceRequestAdapter to be synchronous if built that way,
    // OR we just cheat and assume the default adapter is fine.
    // For strictly correct C code, we need a callback.
    // Let's use the blocking helper if available, or a simple callback hack.
    
    WGPUAdapter adapter = nullptr;
    wgpuInstanceRequestAdapter(mInstance, &adapterOpts, [](WGPURequestAdapterStatus status, WGPUAdapter a, const char* msg, void* userdata) {
        *(WGPUAdapter*)userdata = a;
    }, &adapter);

    if (!adapter) {
        std::cerr << "Failed to get WebGPU Adapter" << std::endl;
        return false;
    }

    // 4. Request Device
    WGPUDeviceDescriptor deviceDesc = {};
    wgpuAdapterRequestDevice(adapter, &deviceDesc, [](WGPURequestDeviceStatus status, WGPUDevice d, const char* msg, void* userdata) {
        *(WGPUDevice*)userdata = d;
    }, &mDevice);

    if (!mDevice) {
        std::cerr << "Failed to get WebGPU Device" << std::endl;
        return false;
    }

    mQueue = wgpuDeviceGetQueue(mDevice);
    
    // Get initial size
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

WGPUTextureView WebGPUContext::getCurrentTextureView() {
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(mSurface, &surfaceTexture);
    
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

    return wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);
}

void WebGPUContext::present() {
    // wgpuSurfacePresent(mSurface); // Not needed in typical loop, handled by browser/Emscripten frame
}
