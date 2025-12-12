/**
 * BespokeSynth WASM - WebGPU Context Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGPUContext.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>

namespace bespoke {
namespace wasm {

WebGPUContext::WebGPUContext() {
    // Initialize default state
    mCurrentState.transform[0] = 1.0f;  // Identity matrix
    mCurrentState.transform[1] = 0.0f;
    mCurrentState.transform[2] = 0.0f;
    mCurrentState.transform[3] = 1.0f;
    mCurrentState.transform[4] = 0.0f;
    mCurrentState.transform[5] = 0.0f;
}

WebGPUContext::~WebGPUContext() {
    if (mRenderPassEncoder) {
        wgpuRenderPassEncoderRelease(mRenderPassEncoder);
    }
    if (mCommandEncoder) {
        wgpuCommandEncoderRelease(mCommandEncoder);
    }
    if (mCurrentTextureView) {
        wgpuTextureViewRelease(mCurrentTextureView);
    }
    if (mSwapChain) {
        wgpuSwapChainRelease(mSwapChain);
    }
    if (mQueue) {
        wgpuQueueRelease(mQueue);
    }
    if (mDevice) {
        wgpuDeviceRelease(mDevice);
    }
    if (mAdapter) {
        wgpuAdapterRelease(mAdapter);
    }
    if (mSurface) {
        wgpuSurfaceRelease(mSurface);
    }
    if (mInstance) {
        wgpuInstanceRelease(mInstance);
    }
}

bool WebGPUContext::initialize(const char* canvasSelector) {
    printf("WebGPUContext: Initializing WebGPU...\n");
    
    // Get WebGPU device from browser
    mDevice = emscripten_webgpu_get_device();
    if (!mDevice) {
        if (mErrorCallback) {
            mErrorCallback("Failed to get WebGPU device from browser");
        }
        printf("WebGPUContext: Failed to get WebGPU device\n");
        return false;
    }
    
    printf("WebGPUContext: Got WebGPU device\n");
    
    // Get the queue
    mQueue = wgpuDeviceGetQueue(mDevice);
    if (!mQueue) {
        if (mErrorCallback) {
            mErrorCallback("Failed to get WebGPU queue");
        }
        return false;
    }
    
    // Create instance (for surface creation)
    mInstance = wgpuCreateInstance(nullptr);
    
    // Create surface from canvas
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = canvasSelector;
    
    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = (WGPUChainedStruct*)&canvasDesc;
    
    mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);
    if (!mSurface) {
        if (mErrorCallback) {
            mErrorCallback("Failed to create WebGPU surface from canvas");
        }
        return false;
    }
    
    printf("WebGPUContext: Created surface from canvas '%s'\n", canvasSelector);
    
    // Get canvas size
    double cssWidth, cssHeight;
    emscripten_get_element_css_size(canvasSelector, &cssWidth, &cssHeight);
    mWidth = static_cast<int>(cssWidth);
    mHeight = static_cast<int>(cssHeight);
    
    printf("WebGPUContext: Canvas size: %dx%d\n", mWidth, mHeight);
    
    // Create swap chain
    createSwapChain();
    
    mInitialized = true;
    printf("WebGPUContext: Initialization complete\n");
    
    return true;
}

void WebGPUContext::createSwapChain() {
    if (mSwapChain) {
        wgpuSwapChainRelease(mSwapChain);
        mSwapChain = nullptr;
    }
    
    WGPUSwapChainDescriptor swapChainDesc = {};
    swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
    swapChainDesc.format = mSwapChainFormat;
    swapChainDesc.width = mWidth;
    swapChainDesc.height = mHeight;
    swapChainDesc.presentMode = WGPUPresentMode_Fifo;
    
    mSwapChain = wgpuDeviceCreateSwapChain(mDevice, mSurface, &swapChainDesc);
    
    printf("WebGPUContext: Created swap chain %dx%d\n", mWidth, mHeight);
}

void WebGPUContext::resize(int width, int height) {
    if (width == mWidth && height == mHeight) {
        return;
    }
    
    mWidth = width;
    mHeight = height;
    
    if (mInitialized) {
        createSwapChain();
    }
}

WGPURenderPassEncoder WebGPUContext::beginFrame() {
    if (!mInitialized) {
        return nullptr;
    }
    
    // Get current texture view from swap chain
    mCurrentTextureView = wgpuSwapChainGetCurrentTextureView(mSwapChain);
    if (!mCurrentTextureView) {
        printf("WebGPUContext: Failed to get current texture view\n");
        return nullptr;
    }
    
    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    mCommandEncoder = wgpuDeviceCreateCommandEncoder(mDevice, &encoderDesc);
    
    // Create render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = mCurrentTextureView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};  // Dark background
    
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    
    mRenderPassEncoder = wgpuCommandEncoderBeginRenderPass(mCommandEncoder, &renderPassDesc);
    
    return mRenderPassEncoder;
}

void WebGPUContext::endFrame() {
    if (!mRenderPassEncoder) {
        return;
    }
    
    // End render pass
    wgpuRenderPassEncoderEnd(mRenderPassEncoder);
    wgpuRenderPassEncoderRelease(mRenderPassEncoder);
    mRenderPassEncoder = nullptr;
    
    // Submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(mCommandEncoder, &cmdBufferDesc);
    
    wgpuQueueSubmit(mQueue, 1, &cmdBuffer);
    
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(mCommandEncoder);
    mCommandEncoder = nullptr;
    
    wgpuTextureViewRelease(mCurrentTextureView);
    mCurrentTextureView = nullptr;
}

void WebGPUContext::handleDeviceLost() {
    printf("WebGPUContext: Device lost, attempting to recover...\n");
    mInitialized = false;
    
    // Attempt to reinitialize
    if (mErrorCallback) {
        mErrorCallback("WebGPU device lost");
    }
}

} // namespace wasm
} // namespace bespoke
