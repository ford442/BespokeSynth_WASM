/**
 * BespokeSynth WASM - Knob Control
 * Skeuomorphic rotary knob UI control for synthesizer parameters
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "WebGPURenderer.h"
#include <string>
#include <functional>

namespace bespoke {
namespace wasm {

/**
 * Knob control styles
 */
enum class KnobStyle {
    Classic,      // Traditional synth knob with pointer
    Vintage,      // Vintage-style with metal cap
    Modern,       // Flat modern style with arc indicator
    LED,          // LED ring around knob
    Minimal       // Minimal dot indicator
};

/**
 * Rotary knob control for synthesizer parameters
 * Provides visual feedback and mouse/touch interaction
 */
class Knob {
public:
    using ValueChangedCallback = std::function<void(float value)>;
    
    Knob(const std::string& label = "", float defaultValue = 0.5f);
    ~Knob() = default;
    
    // Rendering
    void render(WebGPURenderer& renderer, float x, float y, float size);
    
    // Value access
    float getValue() const { return mValue; }
    void setValue(float value);
    void setValueNormalized(float normalized);  // 0-1 range
    float getValueNormalized() const;
    
    // Range settings
    void setRange(float min, float max) { mMin = min; mMax = max; }
    float getMin() const { return mMin; }
    float getMax() const { return mMax; }
    
    // Default value (for double-click reset)
    void setDefaultValue(float value) { mDefaultValue = value; }
    float getDefaultValue() const { return mDefaultValue; }
    
    // Label
    void setLabel(const std::string& label) { mLabel = label; }
    const std::string& getLabel() const { return mLabel; }
    
    // Appearance
    void setStyle(KnobStyle style) { mStyle = style; }
    KnobStyle getStyle() const { return mStyle; }
    
    void setColors(const Color& background, const Color& foreground, const Color& indicator);
    void setBackgroundColor(const Color& color) { mBackgroundColor = color; }
    void setForegroundColor(const Color& color) { mForegroundColor = color; }
    void setIndicatorColor(const Color& color) { mIndicatorColor = color; }
    
    // Bipolar mode (for pan, etc. - center is default)
    void setBipolar(bool bipolar) { mBipolar = bipolar; }
    bool isBipolar() const { return mBipolar; }
    
    // Interaction
    bool hitTest(float x, float y, float knobX, float knobY, float size) const;
    void onMouseDown(float x, float y, float knobX, float knobY, float size);
    void onMouseDrag(float x, float y, float prevX, float prevY);
    void onMouseUp();
    void onDoubleClick();  // Reset to default
    void onScroll(float delta);
    
    // Fine control mode (shift key held)
    void setFineMode(bool fine) { mFineMode = fine; }
    
    // Modulation visualization
    void setModulationAmount(float amount) { mModulationAmount = amount; }
    void setModulationValue(float value) { mModulationValue = value; }
    
    // Callback for value changes
    void setValueChangedCallback(ValueChangedCallback callback) { mValueChangedCallback = callback; }
    
    // Sensitivity
    void setSensitivity(float sensitivity) { mSensitivity = sensitivity; }

    // Value display labeling (name + live formatted value)
    void setUnit(const std::string& unit) { mUnit = unit; }
    const std::string& getUnit() const { return mUnit; }
    void setDisplayPrecision(int precision) { mDisplayPrecision = precision; }
    std::string getDisplayString() const;   // e.g. "440 Hz" or "0.75"

private:
    void renderClassicKnob(WebGPURenderer& renderer, float x, float y, float size);
    void renderVintageKnob(WebGPURenderer& renderer, float x, float y, float size);
    void renderModernKnob(WebGPURenderer& renderer, float x, float y, float size);
    void renderLEDKnob(WebGPURenderer& renderer, float x, float y, float size);
    void renderMinimalKnob(WebGPURenderer& renderer, float x, float y, float size);
    
    float valueToAngle(float value) const;
    void notifyValueChanged();
    
    std::string mLabel;
    float mValue = 0.5f;
    float mDefaultValue = 0.5f;
    float mMin = 0.0f;
    float mMax = 1.0f;
    
    KnobStyle mStyle = KnobStyle::Classic;
    Color mBackgroundColor{0.22f, 0.22f, 0.24f, 1.0f};  // UITheme::kKnobBg
    Color mForegroundColor{0.75f, 0.76f, 0.78f, 1.0f}; // UITheme::kKnobFg
    Color mIndicatorColor{0.95f, 0.96f, 0.98f, 1.0f};  // UITheme::kKnobIndicator
    
    bool mBipolar = false;
    bool mDragging = false;
    bool mFineMode = false;
    float mDragStartValue = 0.0f;
    float mDragStartY = 0.0f;
    float mSensitivity = 0.005f;
    
    float mModulationAmount = 0.0f;
    float mModulationValue = 0.0f;
    
    ValueChangedCallback mValueChangedCallback;
    
    // Animation
    float mAnimatedValue = 0.5f;
    float mAnimationSpeed = 0.1f;

    // Value formatting for labels
    std::string mUnit;
    int mDisplayPrecision = 2;
};

} // namespace wasm
} // namespace bespoke
