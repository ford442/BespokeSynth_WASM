// WidgetPainter (#125 §5): composite widgets recorded through the CPU reference backend, so
// both GPU backends draw them from the same primitive calls.
#include "test_common.h"
#include "BespokeWasm/WidgetPainter.h"
#include "RecordingRenderer.h"

namespace
{
   bool sameColor(const Color& a, const Color& b)
   {
      return std::fabs(a.r - b.r) < 1e-6f && std::fabs(a.g - b.g) < 1e-6f && std::fabs(a.b - b.b) < 1e-6f &&
             std::fabs(a.a - b.a) < 1e-6f;
   }

   void testKnob()
   {
      RecordingRenderer recorder;
      const Color bg(0.1f, 0.2f, 0.3f, 1.0f);
      const Color fg(0.9f, 0.8f, 0.7f, 1.0f);
      WidgetPainter(recorder).drawKnob(50.0f, 40.0f, 10.0f, 0.5f, bg, fg);

      const auto* body = recorder.firstQuad(ShaderKind::KnobHighlight);
      const auto* ticks = recorder.firstQuad(ShaderKind::DialTicks);
      TEST("Knob draws its body and tick quads over the knob square",
           body && ticks && body->x == 40.0f && body->y == 30.0f && body->w == 20.0f && body->h == 20.0f &&
           sameColor(body->color, bg) && sameColor(ticks->color, fg));
      // 12 value-arc segments plus the indicator line, then the tip dot.
      TEST("Knob draws the value arc, indicator and tip", recorder.count("line") == 13 && recorder.count("circle") == 1);

      RecordingRenderer simplified;
      simplified.setDebugMode(WebGLDebugMode::SimplifiedModules);
      WidgetPainter(simplified).drawKnob(50.0f, 40.0f, 10.0f, 0.5f, bg, fg);
      TEST("SimplifiedModules debug mode draws a knob as one filled circle",
           simplified.count("quad") == 0 && simplified.count("circle") == 1 && simplified.count("fill") == 1);
   }

   void testCables()
   {
      RecordingRenderer recorder;
      WidgetPainter painter(recorder);
      painter.drawWire(10.0f, 10.0f, 10.0f, 10.0f, Color(1, 0, 0, 1));
      TEST("A zero-length wire draws nothing", recorder.calls.empty());

      painter.drawWire(0.0f, 0.0f, 30.0f, 40.0f, Color(1, 0, 0, 1), 2.0f);
      const auto* glow = recorder.firstQuad(ShaderKind::WireGlow);
      TEST("A wire is one glow and one pulse quad along its length, inside save/restore",
           glow && std::fabs(glow->w - 50.0f) < 1e-4f && glow->y == -4.0f && glow->h == 8.0f &&
           recorder.countQuads(ShaderKind::ConnectionPulse) == 1 && recorder.count("save") == 1 &&
           recorder.count("restore") == 1);

      recorder.calls.clear();
      painter.drawCableWithSag(0.0f, 0.0f, 100.0f, 0.0f, Color(0, 1, 0, 1), 3.0f, 0.3f);
      float lowest = 0.0f;
      for (const auto& call : recorder.calls)
         if (call.op == "translate")
            lowest = std::max(lowest, call.y);
      TEST("A sagging cable is 20 wire segments", recorder.countQuads(ShaderKind::WireGlow) == 20);
      TEST("The cable sags below its endpoints", lowest > 10.0f && lowest < 30.0f);

      RecordingRenderer debug;
      debug.setDebugMode(WebGLDebugMode::ConnectionDebug);
      WidgetPainter(debug).drawCableWithSag(0.0f, 0.0f, 100.0f, 0.0f, Color(0, 1, 0, 1));
      TEST("ConnectionDebug marks both cable endpoints", debug.count("circle") == 2);
   }

   void testSlidersAndFaders()
   {
      RecordingRenderer recorder;
      WidgetPainter painter(recorder);
      painter.drawSlider(0.0f, 0.0f, 100.0f, 10.0f, 0.25f, Color(0, 0, 0, 1), Color(1, 1, 1, 1));
      const auto* fill = recorder.firstQuad(ShaderKind::SliderFill);
      const auto* handle = recorder.firstQuad(ShaderKind::SliderHandle);
      TEST("A horizontal slider fills to its value", fill && fill->w == 25.0f && fill->h == 10.0f);
      TEST("The slider handle is centred on the value", handle && handle->x == 22.5f && handle->w == 5.0f);

      recorder.calls.clear();
      painter.drawSlider(0.0f, 0.0f, 10.0f, 100.0f, 0.0f, Color(0, 0, 0, 1), Color(1, 1, 1, 1));
      handle = recorder.firstQuad(ShaderKind::SliderHandle);
      TEST("A vertical slider at zero keeps its handle inside the track", handle && handle->y == 95.0f);

      recorder.calls.clear();
      painter.drawFader(0.0f, 0.0f, 10.0f, 20.0f, 1.0f);
      const auto* cap = recorder.firstQuad(ShaderKind::FaderCap);
      TEST("A fader shorter than its cap pins the cap to the bottom", cap && cap->y == -10.0f && cap->h == 30.0f);
   }

   void testMetersAndButtons()
   {
      RecordingRenderer recorder;
      WidgetPainter painter(recorder);
      const Color low(0, 1, 0, 1);
      const Color high(1, 0, 0, 1);
      painter.drawVUMeter(0.0f, 0.0f, 10.0f, 100.0f, 0.35f, low, high);
      int lit = 0;
      for (const auto& call : recorder.calls)
         if (call.op == "quad" && call.kind == ShaderKind::VUMeter && call.color.g > 0.5f)
            ++lit;
      TEST("A VU meter has 10 segments and lights the ones under the level",
           recorder.countQuads(ShaderKind::VUMeter) == 10 && lit == 3);

      recorder.calls.clear();
      recorder.fillColor(Color(0.5f, 0.5f, 0.5f, 1.0f));
      painter.drawButton(0.0f, 0.0f, 40.0f, 20.0f, "Rec", true, false);
      const auto* button = recorder.firstQuad(ShaderKind::Button);
      const RecordingRenderer::Call* label = nullptr;
      for (const auto& call : recorder.calls)
         if (call.op == "text")
            label = &call;
      TEST("A pressed button darkens the current fill color",
           button && sameColor(button->color, Color(0.4f, 0.4f, 0.4f, 1.0f)));
      TEST("A button centres its label", label && label->text == "Rec" && label->w == 11.0f && label->x > 0.0f);

      recorder.calls.clear();
      painter.drawButton(0.0f, 0.0f, 40.0f, 20.0f, "", false, true);
      TEST("A hovered button without a label adds the hover quad and no text",
           recorder.countQuads(ShaderKind::ButtonHover) == 1 && recorder.count("text") == 0);
   }

   void testScopes()
   {
      RecordingRenderer recorder;
      WidgetPainter painter(recorder);
      const float one = 0.0f;
      painter.drawWaveform(0.0f, 0.0f, 100.0f, 50.0f, &one, 1, false);
      TEST("A waveform needs at least two points", recorder.calls.empty());

      const float data[4] = { 0.0f, 1.0f, -1.0f, 0.0f };
      painter.drawScope(0.0f, 0.0f, 90.0f, 50.0f, data, 4);
      TEST("A scope is a grid quad under a stroked trace",
           recorder.countQuads(ShaderKind::ScopeGrid) == 1 && recorder.count("lineTo") == 3 &&
           recorder.count("stroke") == 1 && recorder.count("fill") == 0);

      recorder.calls.clear();
      painter.drawLFOWaveform(0.0f, 0.0f, 90.0f, 50.0f);
      TEST("A filled waveform closes and fills under the trace",
           recorder.count("closePath") == 1 && recorder.count("fill") == 1);

      recorder.calls.clear();
      const float bins[3] = { 0.5f, 1.0f, 0.0f };
      painter.drawSpectrum(0.0f, 0.0f, 30.0f, 100.0f, bins, 3);
      const auto* bar = recorder.firstQuad(ShaderKind::SpectrumBar);
      TEST("A spectrum draws a bar and a peak per bin",
           recorder.countQuads(ShaderKind::SpectrumBar) == 3 && recorder.countQuads(ShaderKind::SpectrumPeak) == 3 &&
           bar && bar->y == 50.0f && bar->h == 50.0f);
   }

   void testResponseCurve()
   {
      RecordingRenderer recorder;
      WidgetPainter painter(recorder);
      const float flat[3] = { 0.0f, 12.0f, -40.0f };
      painter.drawResponseCurve(0.0f, 0.0f, 100.0f, 40.0f, flat, 3, 20.0f, UITheme::kAccentCyan);
      std::vector<const RecordingRenderer::Call*> points;
      for (const auto& call : recorder.calls)
      {
         if (call.op == "moveTo" || call.op == "lineTo")
            points.push_back(&call);
      }
      // 0 dB line, two guides, then the curve's moveTo + 2 lineTo.
      TEST("A response curve draws guides then one point per value", points.size() == 9 && recorder.count("stroke") == 4);
      if (points.size() == 9)
      {
         TEST("Response curve maps 0 dB to the middle, +12 dB up, and clamps below -range",
              points[6]->y == 20.0f && std::fabs(points[7]->y - 8.0f) < 1e-4f && points[8]->y == 40.0f &&
              points[8]->x == 100.0f);
      }
      recorder.calls.clear();
      painter.drawResponseCurve(0.0f, 0.0f, 100.0f, 40.0f, flat, 1, 20.0f, UITheme::kAccentCyan);
      TEST("A one-point response draws only its panel", recorder.count("stroke") == 0);

      auto eq = WasmModuleAdapterRegistry::instance().createModule("eq", 77);
      eq->setPosition(0.0f, 0.0f);
      eq->setControlValue("midGain", 12.0f);
      recorder.calls.clear();
      eq->render(recorder, 0.0f, 0.0f, 1.0f);
      size_t curvePoints = 0;
      for (const auto& call : recorder.calls)
         curvePoints += call.op == "lineTo" ? 1 : 0;
      TEST("EQ draws its response curve through WidgetPainter", curvePoints >= 95);
   }

   void testModuleRendersThroughPainter()
   {
      RecordingRenderer recorder;
      GainModule gain(1);
      gain.setPosition(0.0f, 0.0f);
      gain.render(recorder, 0.0f, 0.0f, 1.0f);
      TEST("Modules draw their widgets through WidgetPainter on any backend",
           recorder.countQuads(ShaderKind::SliderTrack) == 1 && recorder.countQuads(ShaderKind::SliderHandle) == 1);
   }
} // namespace

void test_widget_painter()
{
   printf("\n=== WidgetPainter (#125) ===\n");
   testKnob();
   testCables();
   testSlidersAndFaders();
   testMetersAndButtons();
   testScopes();
   testModuleRendersThroughPainter();
   testResponseCurve();
}
