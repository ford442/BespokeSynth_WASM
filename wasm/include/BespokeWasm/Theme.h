/**
 * BespokeSynth WASM - UI Theme
 * Centralized colors, sizes, and constants for consistent pro-audio visual language
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "WebGPURenderer.h"

namespace bespoke {
namespace wasm {

/**
 * UITheme provides a cohesive dark pro-audio theme with neon accents.
 * Use these instead of magic color values for maintainability and easy theming.
 */
namespace UITheme {

   // === Core Backgrounds (deep dark with subtle depth) ===
   inline const Color kBgDark      { 0.04f, 0.04f, 0.05f, 1.0f };   // Near-black canvas
   inline const Color kBgPanel     { 0.10f, 0.10f, 0.12f, 0.96f };  // Module/panel body
   inline const Color kBgTrack     { 0.18f, 0.18f, 0.20f, 1.0f };   // Slider/knob tracks, inset
   inline const Color kBgElevated  { 0.14f, 0.14f, 0.16f, 1.0f };   // Raised elements

   // === Text ===
   inline const Color kTextPrimary   { 0.92f, 0.93f, 0.95f, 1.0f };
   inline const Color kTextSecondary { 0.65f, 0.66f, 0.70f, 1.0f };
   inline const Color kTextValue     { 0.55f, 0.90f, 1.00f, 1.0f }; // Cyan-ish for live values

   // === Accents (neon / category) ===
   inline const Color kAccentCyan    { 0.00f, 0.88f, 1.00f, 1.0f }; // Audio / primary action
   inline const Color kAccentMagenta { 1.00f, 0.31f, 0.85f, 1.0f }; // Modulation / pulse
   inline const Color kAccentAmber   { 1.00f, 0.75f, 0.25f, 1.0f };
   inline const Color kAccentGreen   { 0.35f, 0.85f, 0.55f, 1.0f };

   // === Knob / Control Materials ===
   inline const Color kKnobBg        { 0.22f, 0.22f, 0.24f, 1.0f };
   inline const Color kKnobFg        { 0.75f, 0.76f, 0.78f, 1.0f };
   inline const Color kKnobIndicator { 0.95f, 0.96f, 0.98f, 1.0f };
   inline const Color kKnobRim       { 0.45f, 0.46f, 0.48f, 1.0f };

   // === Category tints (match ModuleCanvas where possible) ===
   inline const Color kCatInstrument  { 0.20f, 0.55f, 0.85f, 1.0f };
   inline const Color kCatNoteEffect  { 0.65f, 0.35f, 0.75f, 1.0f };
   inline const Color kCatSynth       { 0.30f, 0.72f, 0.55f, 1.0f };
   inline const Color kCatAudioEffect { 0.85f, 0.55f, 0.25f, 1.0f };
   inline const Color kCatModulator   { 0.72f, 0.72f, 0.22f, 1.0f };
   inline const Color kCatPulse       { 0.85f, 0.32f, 0.32f, 1.0f };

   // === Special states ===
   inline const Color kModRing       { 0.30f, 0.72f, 1.00f, 0.85f }; // Cyan modulation arc
   inline const Color kHoverGlow     { 1.00f, 1.00f, 1.00f, 0.15f };
   inline const Color kDisabled      { 0.40f, 0.40f, 0.42f, 0.6f };

   // === Sizes & Spacing (in pixels at 1.0 scale) ===
   inline constexpr float kLabelFontSize   = 11.0f;
   inline constexpr float kValueFontSize   = 9.5f;
   inline constexpr float kKnobLabelOffset = 18.0f;   // Distance below knob center for label
   inline constexpr float kControlGap      = 12.0f;   // Breathing room between controls
   inline constexpr float kPanelPadding    = 10.0f;
   inline constexpr float kTitleBarHeight  = 22.0f;   // Matches ModuleCanvas::kTitleBarHeight

} // namespace UITheme

} // namespace wasm
} // namespace bespoke
