/**
 * BespokeSynth WASM - WebGL2 GLSL shader sources (WGSL parity)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

namespace bespoke {
namespace wasm {

const char* gl2VertexShaderSrc();

const char* gl2SolidFragmentSrc();
const char* gl2KnobHighlightFragmentSrc();
const char* gl2DialTicksFragmentSrc();
const char* gl2WireGlowFragmentSrc();
const char* gl2ConnectionPulseFragmentSrc();
const char* gl2SliderTrackFragmentSrc();
const char* gl2SliderFillFragmentSrc();
const char* gl2SliderHandleFragmentSrc();
const char* gl2ButtonFragmentSrc();
const char* gl2ButtonHoverFragmentSrc();
const char* gl2ToggleSwitchFragmentSrc();
const char* gl2ToggleThumbFragmentSrc();
const char* gl2VUMeterFragmentSrc();
const char* gl2ADSRGridFragmentSrc();
const char* gl2ADSREnvelopeFragmentSrc();
const char* gl2PanelBackgroundFragmentSrc();
const char* gl2PanelBorderedFragmentSrc();
const char* gl2LEDOnFragmentSrc();
const char* gl2LEDOffFragmentSrc();
const char* gl2WaveformFragmentSrc();
const char* gl2WaveformFilledFragmentSrc();
const char* gl2SpectrumBarFragmentSrc();
const char* gl2SpectrumPeakFragmentSrc();
const char* gl2ProgressBarFragmentSrc();
const char* gl2ModWheelFragmentSrc();
const char* gl2ScopeGridFragmentSrc();
const char* gl2FaderGrooveFragmentSrc();
const char* gl2FaderCapFragmentSrc();

const char* gl2PixelTextFragmentPrefix();
const char* gl2PixelTextFragmentSuffix();

} // namespace wasm
} // namespace bespoke
