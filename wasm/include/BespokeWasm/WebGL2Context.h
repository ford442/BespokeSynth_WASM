#pragma once

#include <GLES3/gl3.h>
#include <emscripten/html5.h>

class WebGL2Context
{
public:
   WebGL2Context();
   ~WebGL2Context();

   bool initialize(const char* canvasSelector);
   bool isInitialized() const { return mContext != 0; }

   void resize(int width, int height);
   void beginFrame();
   void endFrame();

   EMSCRIPTEN_WEBGL_CONTEXT_HANDLE getContextHandle() const { return mContext; }

private:
   EMSCRIPTEN_WEBGL_CONTEXT_HANDLE mContext = 0;
   int mWidth = 0;
   int mHeight = 0;
};
