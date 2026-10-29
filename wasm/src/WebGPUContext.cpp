#include "BespokeWasm/WebGPUContext.h"
#include <iostream>
#include <cstdio>
#include <cmath>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cassert>
#include <algorithm>

// --- WebGPU Callbacks ---
// Using the new-style callback API with WGPUCallbackInfo structs

// Forward declarations
class WebGPUContext;
static void handleAdapterRequest(WebGPUContext* context, WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message);
static void handleDeviceRequest(WebGPUContext* context, WGPURequestDeviceStatus status, WGPUDevice device, const char* message);

namespace
{

   // The canvas backing store is sized in physical (device) pixels so rendering
   // is sharp on HiDPI displays; all layout/hit-testing math elsewhere in the
   // app stays in logical (CSS) pixels. See docs/webgl-fallback.md.
   double currentDevicePixelRatio()
   {
      const double dpr = emscripten_get_device_pixel_ratio();
      return dpr > 0.0 ? dpr : 1.0;
   }

   int toPhysicalPixels(int logicalPixels, double dpr)
   {
      return std::max(1, static_cast<int>(std::lround(static_cast<double>(logicalPixels) * dpr)));
   }

} // namespace

// Adapter callback - new style with WGPUStringView
static void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* /*userdata2*/)
{
   std::string msg;
   if (message.data && message.length > 0)
   {
      msg = std::string(message.data, message.length);
   }
   printf("WebGPUContext: onAdapterRequest called, status=%d\n", (int)status);
   handleAdapterRequest(static_cast<WebGPUContext*>(userdata1), status, adapter, msg.c_str());
}

// Device callback - new style with WGPUStringView
static void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* /*userdata2*/)
{
   std::string msg;
   if (message.data && message.length > 0)
   {
      msg = std::string(message.data, message.length);
   }
   printf("WebGPUContext: onDeviceRequest called, status=%d\n", (int)status);
   handleDeviceRequest(static_cast<WebGPUContext*>(userdata1), status, device, msg.c_str());
}

// Device error callback - new style (includes device pointer as first param)
static void deviceErrorCallback(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* /*userdata1*/, void* /*userdata2*/)
{
   (void)device; // Unused
   std::string msg;
   if (message.data && message.length > 0)
   {
      msg = std::string(message.data, message.length);
   }
   printf("WebGPU Device Error (type=%d): %s\n", (int)type, msg.c_str());
}

// Device-lost callback - fires on context loss (GPU driver reset, tab backgrounding
// on some platforms, or explicit device.destroy()). We can't recover the same
// WGPUDevice, so we flag it and let the JS shell decide whether to reload or
// fall back to WebGL2, instead of continuing to submit commands into a dead device.
static void deviceLostCallback(WGPUDevice const* device, WGPUDeviceLostReason reason, WGPUStringView message, void* userdata1, void* userdata2)
{
   (void)device;
   (void)userdata2;
   std::string msg;
   if (message.data && message.length > 0)
   {
      msg = std::string(message.data, message.length);
   }
   printf("WebGPU Device Lost (reason=%d): %s\n", (int)reason, msg.c_str());

   // Releasing the device in releaseGpuResources() resolves this callback synchronously
   // (reason=Destroyed or CallbackCancelled); that teardown is not a loss to recover from.
   // GPUDevice.destroy() from outside (or a driver reset) is.
   auto* context = static_cast<WebGPUContext*>(userdata1);
   if (!context || context->isTearingDown() || reason == WGPUDeviceLostReason_CallbackCancelled ||
       reason == WGPUDeviceLostReason_FailedCreation)
      return;

   context->notifyDeviceLost(msg.c_str());
}

static void onPopErrorScope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
                            void* userdata1, void* userdata2)
{
   (void)userdata2;
   auto* pending = static_cast<WebGPUContext::PendingErrorScopes*>(userdata1);
   if (status != WGPUPopErrorScopeStatus_Success)
      pending->failed = true;
   if (type != WGPUErrorType_NoError)
   {
      pending->failed = true;
      if (pending->message.empty() && message.data && message.length > 0)
         pending->message.assign(message.data, message.length);
   }
   if (--pending->remaining > 0)
      return;

   auto done = std::move(pending->onComplete);
   const bool ok = !pending->failed;
   std::string text = std::move(pending->message);
   delete pending;
   if (done)
      done(ok, text);
}

// --- Implementation ---

static void handleDeviceRequest(WebGPUContext* context, WGPURequestDeviceStatus status, WGPUDevice device, const char* message)
{
   printf("WebGPUContext: handleDeviceRequest called, status=%d\n", (int)status);

   if (status == WGPURequestDeviceStatus_Success)
   {
      printf("WebGPUContext: Device acquired, assigning to context\n");
      if (context)
         context->assignDevice(device);
   }
   else
   {
      std::cerr << "WebGPU Device Error: ";
      if (message)
         std::cerr << message;
      std::cerr << std::endl;
      if (context)
         context->notifyComplete(false);
   }
}

static void handleAdapterRequest(WebGPUContext* context, WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message)
{
   printf("WebGPUContext: handleAdapterRequest called, status=%d\n", (int)status);

   if (status == WGPURequestAdapterStatus_Success)
   {
      if (context)
         context->assignAdapter(adapter);

      if (context && adapter)
      {
         printf("WebGPUContext: Adapter found, requesting device\n");

         // Default limits cover everything the renderer and analyzer create: the
         // largest buffer is the frame's vertex buffer (far below the 256 MiB
         // default), and the FFT workgroup is 256
         // invocations (the default maximum) with 4 KiB of workgroup memory.
         // Requesting every adapter limit instead can make requestDevice fail
         // on multi-adapter machines, so only raise what scales with the user's
         // display: the surface is sized in physical pixels (CSS size *
         // devicePixelRatio) and may exceed the default 8192 on very large
         // displays.
         constexpr uint32_t kDefaultMaxTextureDimension2D = 8192;
         WGPULimits requiredLimits = WGPU_LIMITS_INIT;
         WGPULimits adapterLimits = WGPU_LIMITS_INIT;
         if (wgpuAdapterGetLimits(adapter, &adapterLimits) == WGPUStatus_Success &&
             adapterLimits.maxTextureDimension2D > kDefaultMaxTextureDimension2D)
         {
            requiredLimits.maxTextureDimension2D = adapterLimits.maxTextureDimension2D;
         }

         // No features are required: nothing uses timestamp queries or filtered
         // float32 textures. A consumer (e.g. a timestamp-query frame-time HUD)
         // should enable its feature only when it is switched on.
         WGPUDeviceDescriptor deviceDesc = {};
         deviceDesc.label = WGPUStringView{ "BespokeSynth WASM device", strlen("BespokeSynth WASM device") };
         deviceDesc.requiredLimits = &requiredLimits;

         // Set up error callback in device descriptor
         deviceDesc.uncapturedErrorCallbackInfo.callback = deviceErrorCallback;
         deviceDesc.uncapturedErrorCallbackInfo.userdata1 = context;
         deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

         // Spontaneous: after init nothing pumps wgpuInstanceProcessEvents, so an
         // AllowProcessEvents device-lost callback would never be delivered.
         deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
         deviceDesc.deviceLostCallbackInfo.callback = deviceLostCallback;
         deviceDesc.deviceLostCallbackInfo.userdata1 = context;
         deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;

         // Use new callback info structure for device request
         WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
         deviceCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
         deviceCallbackInfo.callback = onDeviceRequest;
         deviceCallbackInfo.userdata1 = context;
         deviceCallbackInfo.userdata2 = nullptr;

         wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallbackInfo);
      }
   }
   else
   {
      std::cerr << "WebGPU Adapter Error: ";
      if (message)
         std::cerr << message;
      std::cerr << std::endl;
      if (context)
         context->notifyComplete(false);
   }
}

// -------------------------

WebGPUContext::WebGPUContext()
{
   mCurrentState.transform[0] = 1.0f;
   mCurrentState.transform[2] = 0.0f;
   mCurrentState.transform[4] = 0.0f;
   mCurrentState.transform[1] = 0.0f;
   mCurrentState.transform[3] = 1.0f;
   mCurrentState.transform[5] = 0.0f;
   mCurrentState.color[0] = 1.0f;
   mCurrentState.color[1] = 1.0f;
   mCurrentState.color[2] = 1.0f;
   mCurrentState.color[3] = 1.0f;
}

WebGPUContext::~WebGPUContext()
{
   releaseGpuResources();
}

void WebGPUContext::releaseGpuResources()
{
   mTearingDown = true;
   if (mCurrentPass)
   {
      wgpuRenderPassEncoderRelease(mCurrentPass);
      mCurrentPass = nullptr;
   }
   if (mCurrentView)
   {
      wgpuTextureViewRelease(mCurrentView);
      mCurrentView = nullptr;
   }
   if (mCurrentSurfaceTexture)
   {
      wgpuTextureRelease(mCurrentSurfaceTexture);
      mCurrentSurfaceTexture = nullptr;
   }
   if (mCurrentEncoder)
   {
      wgpuCommandEncoderRelease(mCurrentEncoder);
      mCurrentEncoder = nullptr;
   }
   if (mSurface)
   {
      wgpuSurfaceRelease(mSurface);
      mSurface = nullptr;
   }
   if (mDevice)
   {
      wgpuDeviceRelease(mDevice);
      mDevice = nullptr;
   }
   if (mAdapter)
   {
      wgpuAdapterRelease(mAdapter);
      mAdapter = nullptr;
   }
   if (mQueue)
   {
      wgpuQueueRelease(mQueue);
      mQueue = nullptr;
   }
   if (mInstance)
   {
      wgpuInstanceRelease(mInstance);
      mInstance = nullptr;
   }
   mOnComplete = nullptr;
}

bool WebGPUContext::initializeAsync(const char* selector, std::function<void(bool)> onComplete)
{
   mOnComplete = onComplete;

   // 1. Instance
   WGPUInstanceDescriptor instanceDesc = {};
   mInstance = wgpuCreateInstance(&instanceDesc);
   // Synchronous failures only return false; onComplete is reserved for the async outcome.
   if (!mInstance)
   {
      printf("WebGPUContext: ERROR - Failed to create instance\n");
      return false;
   }
   printf("WebGPUContext: Instance created\n");

   // 2. Surface - use the Emscripten-specific surface source
   WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource = {};
   canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
   canvasSource.selector = WGPUStringView{ selector, strlen(selector) };

   WGPUSurfaceDescriptor surfaceDesc = {};
   surfaceDesc.nextInChain = (WGPUChainedStruct*)&canvasSource;
   mSurface = wgpuInstanceCreateSurface(mInstance, &surfaceDesc);

   if (!mSurface)
   {
      std::cerr << "WebGPUContext: Failed to create surface for selector: ";
      if (selector)
         std::cerr << selector;
      std::cerr << std::endl;
      return false;
   }
   printf("WebGPUContext: Surface created for selector: %s\n", selector);

   // 3. Adapter request (asynchronous)
   WGPURequestAdapterOptions adapterOpts = {};
   adapterOpts.compatibleSurface = mSurface;
   // The UI redraws every frame regardless of audio activity, so prefer the
   // discrete/high-performance GPU on multi-GPU (laptop) systems over the
   // low-power default.
   adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

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

void WebGPUContext::pushErrorScopes()
{
   if (!mDevice)
      return;
   wgpuDevicePushErrorScope(mDevice, WGPUErrorFilter_Validation);
   wgpuDevicePushErrorScope(mDevice, WGPUErrorFilter_Internal);
   wgpuDevicePushErrorScope(mDevice, WGPUErrorFilter_OutOfMemory);
}

void WebGPUContext::popErrorScopes(std::function<void(bool, const std::string&)> onComplete)
{
   if (!mDevice)
   {
      if (onComplete)
         onComplete(false, "no device");
      return;
   }
   // Heap-owned: the callbacks resolve after this frame returns (JS promises), and the
   // last one frees it. Pops run innermost first: OutOfMemory, Internal, Validation.
   auto* pending = new PendingErrorScopes();
   pending->remaining = 3;
   pending->onComplete = std::move(onComplete);
   for (int i = 0; i < 3; ++i)
   {
      WGPUPopErrorScopeCallbackInfo info = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
      info.mode = WGPUCallbackMode_AllowSpontaneous;
      info.callback = onPopErrorScope;
      info.userdata1 = pending;
      wgpuDevicePopErrorScope(mDevice, info);
   }
}

void WebGPUContext::processEvents()
{
   // Process pending WebGPU events.
   // On Emscripten, WebGPU callbacks (adapter/device requests) fire via browser
   // Promise resolution in the JS event loop — they do NOT require emscripten_sleep().
   // Using emscripten_sleep(0) here causes Asyncify reentrancy crashes when JS
   // calls bespoke_process_events() from multiple concurrent setInterval timers.
   // wgpuInstanceProcessEvents() is a safe, synchronous, non-Asyncify call.
   if (mInstance)
   {
      wgpuInstanceProcessEvents(mInstance);
   }
}

void WebGPUContext::onDeviceReady()
{
   printf("WebGPUContext: onDeviceReady\n");
   if (!mDevice)
   {
      printf("WebGPUContext: ERROR - Device is null in onDeviceReady\n");
      if (mOnComplete)
         mOnComplete(false);
      return;
   }

   printf("WebGPUContext: Getting device queue...\n");
   mQueue = wgpuDeviceGetQueue(mDevice);
   if (!mQueue)
   {
      printf("WebGPUContext: ERROR - Failed to get device queue\n");
      if (mOnComplete)
         mOnComplete(false);
      return;
   }

   // Error callback was already set in device descriptor

   printf("WebGPUContext: Getting surface capabilities...\n");
   WGPUSurfaceCapabilities caps = {};
   wgpuSurfaceGetCapabilities(mSurface, mAdapter, &caps);
   if (caps.formatCount > 0)
   {
      mFormat = caps.formats[0];
      printf("WebGPUContext: Selected preferred surface format: %d\n", (int)mFormat);
   }
   wgpuSurfaceCapabilitiesFreeMembers(caps);

   // Get current canvas size and configure surface
   printf("WebGPUContext: Configuring surface...\n");
   double w, h;
   emscripten_get_element_css_size("#canvas", &w, &h);
   if (w <= 0 || h <= 0)
   {
      printf("WebGPUContext: WARNING - Invalid canvas size: %.0fx%.0f, using defaults\n", w, h);
      w = 800;
      h = 600;
   }
   resize((int)w, (int)h);

   printf("WebGPUContext: Device ready, initialization complete\n");
   if (mOnComplete)
      mOnComplete(true);
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
   if (mOnComplete)
      mOnComplete(success);
}

// clang-format off
EM_JS(void, bespoke_js_on_device_lost, (), {
    if (typeof window !== 'undefined' && window['__bespoke_on_device_lost'])
        window['__bespoke_on_device_lost']();
});
// clang-format on

void WebGPUContext::notifyDeviceLost(const char* reason)
{
   mDeviceLost = true;
   bespoke_js_on_device_lost();
   (void)reason;
}

void WebGPUContext::resize(int width, int height)
{
   if (!mDevice || !mSurface)
      return;

   // width/height are logical (CSS) pixels; configure the surface at the
   // physical (device) pixel size so the canvas backing store matches the
   // display's actual resolution instead of blurring on HiDPI screens.
   const double dpr = currentDevicePixelRatio();
   mWidth = toPhysicalPixels(width, dpr);
   mHeight = toPhysicalPixels(height, dpr);
   mLogicalWidth = width;
   mLogicalHeight = height;

   // Screenshots read the canvas (toDataURL in the rendering task), so the surface needs
   // no CopySrc. Opaque matches WebGL2 (alpha:false): the page never shows through.
   WGPUSurfaceConfiguration config = {};
   config.device = mDevice;
   config.format = mFormat;
   config.usage = WGPUTextureUsage_RenderAttachment;
   config.width = mWidth;
   config.height = mHeight;
   config.presentMode = WGPUPresentMode_Fifo;
   config.alphaMode = WGPUCompositeAlphaMode_Opaque;

   wgpuSurfaceConfigure(mSurface, &config);
}

void WebGPUContext::reconfigureSurface()
{
   double cssW = 0.0;
   double cssH = 0.0;
   emscripten_get_element_css_size("#canvas", &cssW, &cssH);
   if (cssW > 0 && cssH > 0)
      resize(static_cast<int>(cssW), static_cast<int>(cssH));
   else if (mLogicalWidth > 0 && mLogicalHeight > 0)
      resize(mLogicalWidth, mLogicalHeight);
}

WGPURenderPassEncoder WebGPUContext::beginFrame()
{
   if (!mSurface)
   {
      printf("WebGPUContext: ERROR - Surface is null in beginFrame\n");
      return nullptr;
   }

   if (!mDevice)
   {
      printf("WebGPUContext: ERROR - Device is null in beginFrame\n");
      return nullptr;
   }

   if (mDeviceLost)
      return nullptr;

   if (mNeedsReconfigure)
   {
      // A prior frame reported SuccessSuboptimal (canvas resized, DPR changed,
      // etc.); reconfigure against the current CSS size before requesting a
      // new surface texture, per the WebGPU spec's suboptimal-surface guidance.
      mNeedsReconfigure = false;
      reconfigureSurface();
   }

   WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
   wgpuSurfaceGetCurrentTexture(mSurface, &surfaceTexture);
   if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
       surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Lost)
   {
      // A stale swapchain does not heal by itself: configure it again and retry once.
      if (surfaceTexture.texture)
         wgpuTextureRelease(surfaceTexture.texture);
      reconfigureSurface();
      surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
      wgpuSurfaceGetCurrentTexture(mSurface, &surfaceTexture);
   }

   const bool textureOk = (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                           surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
   if (!textureOk || surfaceTexture.texture == nullptr)
   {
      if (surfaceTexture.texture)
         wgpuTextureRelease(surfaceTexture.texture);
      if (surfaceTexture.status != mLastSurfaceStatus)
         printf("WebGPUContext: WARNING - Failed to get surface texture, status=%d\n", (int)surfaceTexture.status);
      mLastSurfaceStatus = surfaceTexture.status;
      return nullptr;
   }
   mLastSurfaceStatus = surfaceTexture.status;

   if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
      mNeedsReconfigure = true;

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
   if (!mCurrentView)
   {
      printf("WebGPUContext: ERROR - Failed to create texture view\n");
      return nullptr;
   }

   WGPUCommandEncoderDescriptor encoderDesc = {};
   mCurrentEncoder = wgpuDeviceCreateCommandEncoder(mDevice, &encoderDesc);
   if (!mCurrentEncoder)
   {
      printf("WebGPUContext: ERROR - Failed to create command encoder\n");
      if (mCurrentView)
      {
         wgpuTextureViewRelease(mCurrentView);
         mCurrentView = nullptr;
      }
      return nullptr;
   }

   WGPURenderPassColorAttachment colorAttachment = {};
   colorAttachment.view = mCurrentView;
   colorAttachment.loadOp = WGPULoadOp_Clear;
   colorAttachment.storeOp = WGPUStoreOp_Store;
   colorAttachment.clearValue = { 0.1, 0.1, 0.1, 1.0 };
   colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

   WGPURenderPassDescriptor passDesc = {};
   passDesc.colorAttachmentCount = 1;
   passDesc.colorAttachments = &colorAttachment;

   mCurrentPass = wgpuCommandEncoderBeginRenderPass(mCurrentEncoder, &passDesc);
   if (!mCurrentPass)
   {
      printf("WebGPUContext: ERROR - Failed to begin render pass\n");
   }

   return mCurrentPass;
}

void WebGPUContext::endFrame()
{
   if (mCurrentPass)
   {
      wgpuRenderPassEncoderEnd(mCurrentPass);
      wgpuRenderPassEncoderRelease(mCurrentPass);
      mCurrentPass = nullptr;
   }

   if (mCurrentEncoder)
   {
      WGPUCommandBufferDescriptor cmdBufDesc = {};
      WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(mCurrentEncoder, &cmdBufDesc);
      wgpuQueueSubmit(mQueue, 1, &cmdBuf);
      wgpuCommandBufferRelease(cmdBuf);
      wgpuCommandEncoderRelease(mCurrentEncoder);
      mCurrentEncoder = nullptr;
   }

   if (mCurrentView)
   {
      wgpuTextureViewRelease(mCurrentView);
      mCurrentView = nullptr;
   }

   if (mCurrentSurfaceTexture)
   {
      wgpuTextureRelease(mCurrentSurfaceTexture);
      mCurrentSurfaceTexture = nullptr;
   }
}
