/**
 * BespokeSynth WASM - WebGL2 Context
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "WebGL2Context.h"
#include <cstdio>

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
   if (mContext)
      return true;

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

   mContext = emscripten_webgl_create_context(canvasSelector, &attrs);
   if (mContext <= 0)
   {
      printf("WebGL2Context: failed to create context (error %d)\n", mContext);
      mContext = 0;
      return false;
   }

   if (emscripten_webgl_make_context_current(mContext) != EMSCRIPTEN_RESULT_SUCCESS)
   {
      printf("WebGL2Context: failed to make context current\n");
      emscripten_webgl_destroy_context(mContext);
      mContext = 0;
      return false;
   }

   printf("WebGL2Context: context ready\n");
   return true;
}

void WebGL2Context::resize(int width, int height)
{
   mWidth = width;
   mHeight = height;
}

void WebGL2Context::beginFrame()
{
   if (!mContext)
      return;

   emscripten_webgl_make_context_current(mContext);
   glViewport(0, 0, mWidth, mHeight);
   glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void WebGL2Context::endFrame()
{
   // Canvas is presented automatically by Emscripten each frame.
}
