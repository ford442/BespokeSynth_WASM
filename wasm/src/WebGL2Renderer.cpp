/**
 * BespokeSynth WASM - WebGL2 Renderer Implementation
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/WebGL2Renderer.h"
#include "BespokeWasm/WebGL2Shaders.h"
#include "BespokeWasm/Theme.h"
#include "BespokeWasm/PixelFont.h"
#include "BespokeWasm/SdfFont.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace bespoke {
namespace wasm {

WebGL2Renderer::WebGL2Renderer(WebGL2Context& context)
: mContext(context)
{
   reset();
}

WebGL2Renderer::~WebGL2Renderer()
{
   if (mVertexBuffer)
      glDeleteBuffers(1, &mVertexBuffer);
   if (mVertexArray)
      glDeleteVertexArrays(1, &mVertexArray);
   if (mAtlasTexture)
      glDeleteTextures(1, &mAtlasTexture);
   for (GL2Program& prog : mPrograms)
   {
      if (prog.program)
         glDeleteProgram(prog.program);
   }
   if (mSharedVertexShader)
      glDeleteShader(mSharedVertexShader);
}

GLuint WebGL2Renderer::compileShader(GLenum type, const char* source)
{
   GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);

   GLint ok = 0;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      char log[1024];
      glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
      printf("WebGL2Renderer: shader compile error: %s\n", log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

GLuint WebGL2Renderer::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
   GLuint program = glCreateProgram();
   glAttachShader(program, vertexShader);
   glAttachShader(program, fragmentShader);
   glLinkProgram(program);

   GLint ok = 0;
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok)
   {
      char log[1024];
      glGetProgramInfoLog(program, sizeof(log), nullptr, log);
      printf("WebGL2Renderer: program link error: %s\n", log);
      glDeleteProgram(program);
      return 0;
   }
   return program;
}

bool WebGL2Renderer::buildProgram(GLPipelineKind kind, const char* fragmentSrc, bool needsTime)
{
   GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
   if (!fs)
      return false;

   GLuint program = linkProgram(mSharedVertexShader, fs);
   glDeleteShader(fs);
   if (!program)
      return false;

   GL2Program& slot = mPrograms[static_cast<size_t>(kind)];
   slot.program = program;
   slot.uViewSize = glGetUniformLocation(program, "uViewSize");
   slot.uTime = needsTime ? glGetUniformLocation(program, "uTime") : -1;
   return true;
}

void WebGL2Renderer::createPrograms()
{
   mSharedVertexShader = compileShader(GL_VERTEX_SHADER, gl2VertexShaderSrc());
   if (!mSharedVertexShader)
      return;

   buildProgram(GLPipelineKind::Solid, gl2SolidFragmentSrc(), false);
   buildProgram(GLPipelineKind::KnobHighlight, gl2KnobHighlightFragmentSrc(), false);
   buildProgram(GLPipelineKind::DialTicks, gl2DialTicksFragmentSrc(), false);
   buildProgram(GLPipelineKind::WireGlow, gl2WireGlowFragmentSrc(), false);
   buildProgram(GLPipelineKind::ConnectionPulse, gl2ConnectionPulseFragmentSrc(), true);
   buildProgram(GLPipelineKind::SliderTrack, gl2SliderTrackFragmentSrc(), false);
   buildProgram(GLPipelineKind::SliderFill, gl2SliderFillFragmentSrc(), true);
   buildProgram(GLPipelineKind::SliderHandle, gl2SliderHandleFragmentSrc(), false);
   buildProgram(GLPipelineKind::Button, gl2ButtonFragmentSrc(), false);
   buildProgram(GLPipelineKind::ButtonHover, gl2ButtonHoverFragmentSrc(), true);
   buildProgram(GLPipelineKind::ToggleSwitch, gl2ToggleSwitchFragmentSrc(), false);
   buildProgram(GLPipelineKind::ToggleThumb, gl2ToggleThumbFragmentSrc(), false);
   buildProgram(GLPipelineKind::VUMeter, gl2VUMeterFragmentSrc(), false);
   buildProgram(GLPipelineKind::ADSRGrid, gl2ADSRGridFragmentSrc(), false);
   buildProgram(GLPipelineKind::ADSREnvelope, gl2ADSREnvelopeFragmentSrc(), false);
   buildProgram(GLPipelineKind::PanelBackground, gl2PanelBackgroundFragmentSrc(), false);
   buildProgram(GLPipelineKind::PanelBordered, gl2PanelBorderedFragmentSrc(), false);
   buildProgram(GLPipelineKind::LEDOn, gl2LEDOnFragmentSrc(), false);
   buildProgram(GLPipelineKind::LEDOff, gl2LEDOffFragmentSrc(), false);
   buildProgram(GLPipelineKind::Waveform, gl2WaveformFragmentSrc(), false);
   buildProgram(GLPipelineKind::WaveformFilled, gl2WaveformFilledFragmentSrc(), false);
   buildProgram(GLPipelineKind::SpectrumBar, gl2SpectrumBarFragmentSrc(), false);
   buildProgram(GLPipelineKind::SpectrumPeak, gl2SpectrumPeakFragmentSrc(), false);
   buildProgram(GLPipelineKind::ProgressBar, gl2ProgressBarFragmentSrc(), true);
   buildProgram(GLPipelineKind::ModWheel, gl2ModWheelFragmentSrc(), false);
   buildProgram(GLPipelineKind::ScopeGrid, gl2ScopeGridFragmentSrc(), false);
   buildProgram(GLPipelineKind::FaderGroove, gl2FaderGrooveFragmentSrc(), false);
   buildProgram(GLPipelineKind::FaderCap, gl2FaderCapFragmentSrc(), false);

   std::string pixelTextSrc = gl2PixelTextFragmentPrefix();
   pixelTextSrc += std::to_string(kPixelFontGlyphCount);
   pixelTextSrc += gl2PixelTextFragmentSuffix();
   buildProgram(GLPipelineKind::PixelText, pixelTextSrc.c_str(), false);

   const GLint fontColsLoc = glGetUniformLocation(programFor(GLPipelineKind::PixelText).program, "uFontCols");
   if (fontColsLoc >= 0)
   {
      const uint32_t* cols = getPixelFontColumns();
      std::vector<GLint> fontData(kPixelFontGlyphCount * kPixelFontColumnsPerGlyph);
      for (size_t i = 0; i < fontData.size(); ++i)
         fontData[i] = static_cast<GLint>(cols[i]);
      glUseProgram(programFor(GLPipelineKind::PixelText).program);
      glUniform1iv(fontColsLoc, static_cast<GLsizei>(fontData.size()), fontData.data());
   }

   buildProgram(GLPipelineKind::SdfText, gl2SdfTextFragmentSrc(), false);
}

void WebGL2Renderer::createTextAtlas()
{
   const SdfFont& font = uiSdfFont();
   const GLuint program = programFor(GLPipelineKind::SdfText).program;
   if (!font.isLoaded() || !program)
      return;

   glGenTextures(1, &mAtlasTexture);
   glBindTexture(GL_TEXTURE_2D, mAtlasTexture);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font.atlasWidth(), font.atlasHeight(), 0, GL_RED, GL_UNSIGNED_BYTE,
                font.atlasPixels());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glBindTexture(GL_TEXTURE_2D, 0);

   // Atlas parameters never change, so set them once.
   glUseProgram(program);
   glUniform1i(glGetUniformLocation(program, "uAtlas"), 0);
   glUniform2f(glGetUniformLocation(program, "uAtlasSize"), static_cast<float>(font.atlasWidth()),
               static_cast<float>(font.atlasHeight()));
   glUniform1f(glGetUniformLocation(program, "uSdfPxRange"), font.pxRange());
   glUseProgram(0);
}

bool WebGL2Renderer::sdfTextActive() const
{
   return mAtlasTexture != 0 && useSdfTextForFace(mFontName.c_str());
}

const WebGL2Renderer::GL2Program& WebGL2Renderer::programFor(GLPipelineKind kind) const
{
   return mPrograms[static_cast<size_t>(kind)];
}

void WebGL2Renderer::createBuffers()
{
   glGenVertexArrays(1, &mVertexArray);
   glGenBuffers(1, &mVertexBuffer);

   glBindVertexArray(mVertexArray);
   glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex2D) * 65536, nullptr, GL_DYNAMIC_DRAW);

   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(0));
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(offsetof(Vertex2D, u)));
   glEnableVertexAttribArray(2);
   glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(offsetof(Vertex2D, color)));

   glBindVertexArray(0);
}

bool WebGL2Renderer::initialize()
{
   if (!mContext.isInitialized())
      return false;

   emscripten_webgl_make_context_current(mContext.getContextHandle());
   createPrograms();
   createBuffers();
   createTextAtlas();
   return programFor(GLPipelineKind::Solid).program != 0
      && programFor(GLPipelineKind::PixelText).program != 0;
}

void WebGL2Renderer::beginFrame(int width, int height, float pixelRatio, float time)
{
   mWidth = width;
   mHeight = height;
   mPixelRatio = pixelRatio;
   mTime = time;

   mVertices.clear();
   mDrawCalls.clear();
   mCurrentBatchFirstVertex = 0;
   mCurrentPipeline = GLPipelineKind::Solid;

   mContext.beginFrame();
   reset();
}

void WebGL2Renderer::flushBatch()
{
   const uint32_t batchCount = static_cast<uint32_t>(mVertices.size()) - mCurrentBatchFirstVertex;
   if (batchCount > 0)
   {
      DrawCall dc;
      dc.pipeline = mCurrentPipeline;
      dc.firstVertex = mCurrentBatchFirstVertex;
      dc.vertexCount = batchCount;
      dc.hasScissor = mCurrentState.hasScissor;
      if (dc.hasScissor)
      {
         dc.scissor[0] = mCurrentState.scissor[0];
         dc.scissor[1] = mCurrentState.scissor[1];
         dc.scissor[2] = mCurrentState.scissor[2];
         dc.scissor[3] = mCurrentState.scissor[3];
      }
      mDrawCalls.push_back(dc);
   }
   mCurrentBatchFirstVertex = static_cast<uint32_t>(mVertices.size());
}

void WebGL2Renderer::setPipeline(GLPipelineKind pipeline)
{
   if (mCurrentPipeline != pipeline)
   {
      flushBatch();
      mCurrentPipeline = pipeline;
   }
}

void WebGL2Renderer::pushVertex(float x, float y, float u, float v, const Color& color)
{
   Vertex2D vtx;
   vtx.x = x;
   vtx.y = y;
   vtx.u = u;
   vtx.v = v;
   vtx.color = color;
   mVertices.push_back(vtx);
}

void WebGL2Renderer::transformPoint(float& x, float& y)
{
   const float* t = mCurrentState.transform;
   const float nx = t[0] * x + t[2] * y + t[4];
   const float ny = t[1] * x + t[3] * y + t[5];
   x = nx;
   y = ny;
}

void WebGL2Renderer::endFrame()
{
   flushBatch();

   if (mVertices.empty())
   {
      mContext.endFrame();
      return;
   }

   glBindVertexArray(mVertexArray);
   glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
   glBufferSubData(GL_ARRAY_BUFFER, 0, mVertices.size() * sizeof(Vertex2D), mVertices.data());

   const float viewSize[2] = { static_cast<float>(mWidth), static_cast<float>(mHeight) };

   // dc.scissor is authored in the same logical (CSS) pixel space as mWidth/mHeight,
   // but glScissor operates on the actual (physical) framebuffer set up by
   // WebGL2Context::resize(); scale by the device pixel ratio so clipping stays
   // aligned with the higher-resolution backing store on HiDPI displays.
   const float dpr = mPixelRatio > 0.0f ? mPixelRatio : 1.0f;

   glDisable(GL_SCISSOR_TEST); // default for the frame
   for (const DrawCall& dc : mDrawCalls)
   {
      if (dc.vertexCount == 0)
         continue;

      if (dc.hasScissor)
      {
         glEnable(GL_SCISSOR_TEST);
         GLint sx = static_cast<GLint>(dc.scissor[0] * dpr);
         GLint sy = static_cast<GLint>((static_cast<float>(mHeight) - (dc.scissor[1] + dc.scissor[3])) * dpr);
         GLint sw = static_cast<GLint>(dc.scissor[2] * dpr);
         GLint sh = static_cast<GLint>(dc.scissor[3] * dpr);
         if (sw < 0) sw = 0;
         if (sh < 0) sh = 0;
         glScissor(sx, sy, sw, sh);
      }
      else
      {
         glDisable(GL_SCISSOR_TEST);
      }

      const GL2Program& prog = programFor(dc.pipeline);
      if (!prog.program)
         continue;

      glUseProgram(prog.program);
      if (dc.pipeline == GLPipelineKind::SdfText)
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, mAtlasTexture);
      }
      if (prog.uViewSize >= 0)
         glUniform2fv(prog.uViewSize, 1, viewSize);
      if (prog.uTime >= 0)
         glUniform1f(prog.uTime, mTime);

      glDrawArrays(GL_TRIANGLES, dc.firstVertex, dc.vertexCount);
   }

   glDisable(GL_SCISSOR_TEST);
   glBindVertexArray(0);
   mContext.endFrame();
}

// Factory (declared in Renderer2D.h)
std::unique_ptr<Renderer2D> createWebGL2Renderer(WebGL2Context& context)
{
   return std::make_unique<WebGL2Renderer>(context);
}

} // namespace wasm
} // namespace bespoke
