#pragma once

#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include <string>

struct WebGL2Capabilities
{
   bool webgl2Supported = false;
   bool vertexArrayObject = false;
   bool instancedArrays = false;
   bool blendMinMax = false;
   bool elementIndexUint = false;
   int maxTextureSize = 0;
   int maxVertexAttribs = 0;
   int maxVertexUniformVectors = 0;
   int maxFragmentUniformVectors = 0;
   int maxRenderbufferSize = 0;
};

class WebGL2Context
{
public:
   WebGL2Context();
   ~WebGL2Context();

   // Probe browser/canvas support without leaving a context active.
   static bool probeSupport(const char* canvasSelector = "#canvas");
   static const char* getLastError();
   static void setPreserveDrawingBuffer(bool enabled);

   bool initialize(const char* canvasSelector);
   bool isInitialized() const { return mContext != 0; }

   void makeCurrent();
   void resize(int width, int height);
   void beginFrame();
   void endFrame();

   const WebGL2Capabilities& getCapabilities() const { return mCapabilities; }
   const char* getCanvasSelector() const { return mCanvasSelector.c_str(); }

   EMSCRIPTEN_WEBGL_CONTEXT_HANDLE getContextHandle() const { return mContext; }

private:
   bool queryCapabilities();
   static void setError(const char* message);

   EMSCRIPTEN_WEBGL_CONTEXT_HANDLE mContext = 0;
   std::string mCanvasSelector;
   WebGL2Capabilities mCapabilities;
   int mWidth = 0;
   int mHeight = 0;
};
