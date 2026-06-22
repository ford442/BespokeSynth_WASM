/**
 * BespokeSynth WASM - WebGL2 Context
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGL2Context.h"

#include <cstdio>
#include <cstring>

namespace {

thread_local std::string gLastWebGL2Error;

const char* emscriptenResultToString(int result)
{
   switch (result)
   {
      case EMSCRIPTEN_RESULT_SUCCESS: return "success";
      case EMSCRIPTEN_RESULT_DEFERRED: return "deferred";
      case EMSCRIPTEN_RESULT_NOT_SUPPORTED: return "not supported";
      case EMSCRIPTEN_RESULT_FAILED_NOT_DEFERRED: return "failed (not deferred)";
      case EMSCRIPTEN_RESULT_INVALID_TARGET: return "invalid canvas selector";
      case EMSCRIPTEN_RESULT_UNKNOWN_TARGET: return "canvas element not found";
      case EMSCRIPTEN_RESULT_INVALID_PARAM: return "invalid parameter";
      case EMSCRIPTEN_RESULT_FAILED: return "operation failed";
      case EMSCRIPTEN_RESULT_NO_DATA: return "no data";
      default: return "unknown error";
   }
}

const char* webglCreateContextError(int errorCode)
{
   switch (errorCode)
   {
      case EMSCRIPTEN_RESULT_NOT_SUPPORTED:
         return "WebGL2 is not supported in this browser";
      case EMSCRIPTEN_RESULT_UNKNOWN_TARGET:
         return "Canvas element not found — ensure #canvas exists before bespoke_init()";
      case EMSCRIPTEN_RESULT_INVALID_TARGET:
         return "Invalid canvas selector";
      case EMSCRIPTEN_RESULT_INVALID_PARAM:
         return "Invalid WebGL context attributes";
      default:
         return "WebGL2 context creation failed — the canvas may already be bound to WebGPU";
   }
}

bool extensionSupported(const char* name)
{
   return emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(), name) == EMSCRIPTEN_RESULT_SUCCESS;
}

} // namespace

const char* WebGL2Context::getLastError()
{
   return gLastWebGL2Error.c_str();
}

void WebGL2Context::setError(const char* message)
{
   gLastWebGL2Error = message ? message : "";
   if (!gLastWebGL2Error.empty())
      printf("WebGL2Context: %s\n", gLastWebGL2Error.c_str());
}

bool WebGL2Context::probeSupport(const char* canvasSelector)
{
   gLastWebGL2Error.clear();

   if (!canvasSelector || canvasSelector[0] == '\0')
   {
      setError("Canvas selector is empty");
      return false;
   }

   EmscriptenWebGLContextAttributes attrs;
   emscripten_webgl_init_context_attributes(&attrs);
   attrs.majorVersion = 2;
   attrs.minorVersion = 0;
   attrs.alpha = EM_TRUE;
   attrs.depth = EM_FALSE;
   attrs.stencil = EM_FALSE;
   attrs.antialias = EM_TRUE;
   attrs.premultipliedAlpha = EM_TRUE;
   attrs.preserveDrawingBuffer = EM_TRUE;
   attrs.enableExtensionsByDefault = EM_TRUE;

   const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE probeContext =
      emscripten_webgl_create_context(canvasSelector, &attrs);
   if (probeContext <= 0)
   {
      char message[256];
      snprintf(message, sizeof(message),
               "%s (Emscripten code %ld: %s)",
               webglCreateContextError(static_cast<int>(probeContext)),
               static_cast<long>(probeContext),
               emscriptenResultToString(static_cast<int>(probeContext)));
      setError(message);
      return false;
   }

   emscripten_webgl_destroy_context(probeContext);
   return true;
}

WebGL2Context::WebGL2Context() = default;

WebGL2Context::~WebGL2Context()
{
   if (mContext)
   {
      emscripten_webgl_destroy_context(mContext);
      mContext = 0;
   }
}

bool WebGL2Context::initialize(const char* canvasSelector)
{
   gLastWebGL2Error.clear();

   if (mContext)
      return true;

   if (!canvasSelector || canvasSelector[0] == '\0')
   {
      setError("Canvas selector is empty");
      return false;
   }

   mCanvasSelector = canvasSelector;

   if (!probeSupport(canvasSelector))
      return false;

   EmscriptenWebGLContextAttributes attrs;
   emscripten_webgl_init_context_attributes(&attrs);
   attrs.majorVersion = 2;
   attrs.minorVersion = 0;
   attrs.alpha = EM_TRUE;
   attrs.depth = EM_FALSE;
   attrs.stencil = EM_FALSE;
   attrs.antialias = EM_TRUE;
   attrs.premultipliedAlpha = EM_TRUE;
   attrs.preserveDrawingBuffer = EM_TRUE;
   attrs.enableExtensionsByDefault = EM_TRUE;
   attrs.explicitSwapControl = EM_FALSE;
   attrs.renderViaOffscreenBackBuffer = EM_FALSE;

   mContext = emscripten_webgl_create_context(canvasSelector, &attrs);
   if (mContext <= 0)
   {
      char message[256];
      snprintf(message, sizeof(message),
               "%s (Emscripten code %ld: %s)",
               webglCreateContextError(static_cast<int>(mContext)),
               static_cast<long>(mContext),
               emscriptenResultToString(static_cast<int>(mContext)));
      setError(message);
      mContext = 0;
      return false;
   }

   const int makeCurrentResult = emscripten_webgl_make_context_current(mContext);
   if (makeCurrentResult != EMSCRIPTEN_RESULT_SUCCESS)
   {
      char message[256];
      snprintf(message, sizeof(message),
               "Failed to bind WebGL2 context to canvas (Emscripten: %s)",
               emscriptenResultToString(makeCurrentResult));
      setError(message);
      emscripten_webgl_destroy_context(mContext);
      mContext = 0;
      return false;
   }

   if (!queryCapabilities())
   {
      emscripten_webgl_destroy_context(mContext);
      mContext = 0;
      return false;
   }

   double cssWidth = 0.0;
   double cssHeight = 0.0;
   emscripten_get_element_css_size(canvasSelector, &cssWidth, &cssHeight);
   if (cssWidth <= 0.0 || cssHeight <= 0.0)
   {
      cssWidth = 800.0;
      cssHeight = 600.0;
      printf("WebGL2Context: WARNING - invalid canvas size, using 800x600 defaults\n");
   }
   resize(static_cast<int>(cssWidth), static_cast<int>(cssHeight));

   printf("WebGL2Context: context ready (%dx%d, maxTexture=%d)\n",
          mWidth, mHeight, mCapabilities.maxTextureSize);
   return true;
}

bool WebGL2Context::queryCapabilities()
{
   mCapabilities = WebGL2Capabilities{};
   mCapabilities.webgl2Supported = true;

   // Core WebGL2 features required by the renderer.
   mCapabilities.vertexArrayObject = true;
   mCapabilities.instancedArrays = true;
   mCapabilities.elementIndexUint = true;

   GLint majorVersion = 0;
   GLint minorVersion = 0;
   glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
   glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
   if (majorVersion < 2)
   {
      setError("WebGL2 context reports OpenGL ES version < 2.0");
      return false;
   }

   glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mCapabilities.maxTextureSize);
   glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &mCapabilities.maxVertexAttribs);
   glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &mCapabilities.maxVertexUniformVectors);
   glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &mCapabilities.maxFragmentUniformVectors);
   glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &mCapabilities.maxRenderbufferSize);

   mCapabilities.blendMinMax = extensionSupported("EXT_blend_minmax");

   if (mCapabilities.maxTextureSize <= 0 || mCapabilities.maxVertexAttribs < 8)
   {
      setError("WebGL2 context failed capability query — driver may be blocked or unavailable");
      return false;
   }

   printf("WebGL2Context: capabilities — VAO=%d instancing=%d blendMinMax=%d "
          "maxTexture=%d maxAttribs=%d\n",
          mCapabilities.vertexArrayObject ? 1 : 0,
          mCapabilities.instancedArrays ? 1 : 0,
          mCapabilities.blendMinMax ? 1 : 0,
          mCapabilities.maxTextureSize,
          mCapabilities.maxVertexAttribs);

   return true;
}

void WebGL2Context::makeCurrent()
{
   if (!mContext)
      return;

   const int result = emscripten_webgl_make_context_current(mContext);
   if (result != EMSCRIPTEN_RESULT_SUCCESS)
   {
      char message[128];
      snprintf(message, sizeof(message),
               "Failed to make WebGL2 context current (%s)",
               emscriptenResultToString(result));
      setError(message);
   }
}

void WebGL2Context::resize(int width, int height)
{
   mWidth = width;
   mHeight = height;

   if (!mContext)
      return;

   makeCurrent();
   glViewport(0, 0, mWidth, mHeight);
}

void WebGL2Context::beginFrame()
{
   if (!mContext)
      return;

   makeCurrent();
   glViewport(0, 0, mWidth, mHeight);
   glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void WebGL2Context::endFrame()
{
   // The browser presents the default framebuffer automatically each frame.
}
