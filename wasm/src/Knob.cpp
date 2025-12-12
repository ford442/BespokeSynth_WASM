/**
 * BespokeSynth WASM - Knob Control Implementation
 * 
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "Knob.h"
#include <cmath>
#include <algorithm>

namespace bespoke {
namespace wasm {

static const float kPi = 3.14159265f;
static const float kStartAngle = 0.75f * kPi;  // 135 degrees
static const float kEndAngle = 2.25f * kPi;     // 405 degrees (wraps around)
static const float kAngleRange = 1.5f * kPi;    // 270 degrees total range

Knob::Knob(const std::string& label, float defaultValue)
    : mLabel(label)
    , mValue(defaultValue)
    , mDefaultValue(defaultValue)
    , mAnimatedValue(defaultValue)
{
}

void Knob::setValue(float value) {
    value = std::max(mMin, std::min(mMax, value));
    if (value != mValue) {
        mValue = value;
        notifyValueChanged();
    }
}

void Knob::setValueNormalized(float normalized) {
    normalized = std::max(0.0f, std::min(1.0f, normalized));
    setValue(mMin + normalized * (mMax - mMin));
}

float Knob::getValueNormalized() const {
    if (mMax == mMin) return 0.0f;
    return (mValue - mMin) / (mMax - mMin);
}

void Knob::setColors(const Color& background, const Color& foreground, const Color& indicator) {
    mBackgroundColor = background;
    mForegroundColor = foreground;
    mIndicatorColor = indicator;
}

float Knob::valueToAngle(float value) const {
    float normalized = (value - mMin) / (mMax - mMin);
    return kStartAngle + normalized * kAngleRange;
}

void Knob::notifyValueChanged() {
    if (mValueChangedCallback) {
        mValueChangedCallback(mValue);
    }
}

void Knob::render(WebGPURenderer& renderer, float x, float y, float size) {
    // Smooth animation
    mAnimatedValue += (mValue - mAnimatedValue) * mAnimationSpeed;
    
    switch (mStyle) {
        case KnobStyle::Classic:
            renderClassicKnob(renderer, x, y, size);
            break;
        case KnobStyle::Vintage:
            renderVintageKnob(renderer, x, y, size);
            break;
        case KnobStyle::Modern:
            renderModernKnob(renderer, x, y, size);
            break;
        case KnobStyle::LED:
            renderLEDKnob(renderer, x, y, size);
            break;
        case KnobStyle::Minimal:
            renderMinimalKnob(renderer, x, y, size);
            break;
    }
    
    // Draw label if present
    if (!mLabel.empty()) {
        renderer.fillColor(mForegroundColor);
        renderer.fontSize(size * 0.2f);
        float labelWidth = renderer.textWidth(mLabel.c_str());
        renderer.text(x - labelWidth / 2, y + size * 0.7f, mLabel.c_str());
    }
}

void Knob::renderClassicKnob(WebGPURenderer& renderer, float x, float y, float size) {
    float radius = size * 0.4f;
    float normalizedValue = getValueNormalized();
    
    // Draw shadow
    renderer.fillColor(Color(0.0f, 0.0f, 0.0f, 0.3f));
    renderer.circle(x + 2, y + 2, radius);
    renderer.fill();
    
    // Draw main knob body
    renderer.fillColor(mBackgroundColor);
    renderer.circle(x, y, radius);
    renderer.fill();
    
    // Draw 3D effect - highlight
    Color highlight(
        std::min(1.0f, mBackgroundColor.r + 0.2f),
        std::min(1.0f, mBackgroundColor.g + 0.2f),
        std::min(1.0f, mBackgroundColor.b + 0.2f),
        1.0f
    );
    renderer.strokeColor(highlight);
    renderer.strokeWidth(2.0f);
    renderer.beginPath();
    renderer.arc(x, y, radius * 0.95f, -kPi * 0.75f, kPi * 0.25f, 0);
    renderer.stroke();
    
    // Draw 3D effect - shadow
    Color shadow(
        mBackgroundColor.r * 0.6f,
        mBackgroundColor.g * 0.6f,
        mBackgroundColor.b * 0.6f,
        1.0f
    );
    renderer.strokeColor(shadow);
    renderer.beginPath();
    renderer.arc(x, y, radius * 0.95f, kPi * 0.25f, kPi * 1.25f, 0);
    renderer.stroke();
    
    // Draw outer ring
    renderer.strokeColor(mForegroundColor);
    renderer.strokeWidth(1.0f);
    renderer.circle(x, y, radius);
    renderer.stroke();
    
    // Draw indicator line
    float angle = valueToAngle(mAnimatedValue);
    float innerRadius = radius * 0.3f;
    float outerRadius = radius * 0.85f;
    
    float x1 = x + cosf(angle) * innerRadius;
    float y1 = y + sinf(angle) * innerRadius;
    float x2 = x + cosf(angle) * outerRadius;
    float y2 = y + sinf(angle) * outerRadius;
    
    renderer.strokeColor(mIndicatorColor);
    renderer.strokeWidth(3.0f);
    renderer.line(x1, y1, x2, y2);
    
    // Draw modulation ring if modulation is active
    if (std::abs(mModulationAmount) > 0.001f) {
        float modValue = std::max(mMin, std::min(mMax, mValue + mModulationValue));
        float modAngle = valueToAngle(modValue);
        
        renderer.strokeColor(Color(0.3f, 0.7f, 1.0f, 0.8f));
        renderer.strokeWidth(4.0f);
        renderer.beginPath();
        if (mModulationAmount > 0) {
            renderer.arc(x, y, radius + 5, angle, modAngle, 0);
        } else {
            renderer.arc(x, y, radius + 5, modAngle, angle, 0);
        }
        renderer.stroke();
    }
}

void Knob::renderVintageKnob(WebGPURenderer& renderer, float x, float y, float size) {
    float radius = size * 0.4f;
    
    // Outer ring (metal)
    renderer.fillColor(Color(0.4f, 0.4f, 0.42f, 1.0f));
    renderer.circle(x, y, radius);
    renderer.fill();
    
    // Metal texture with knurling
    renderer.strokeColor(Color(0.5f, 0.5f, 0.52f, 1.0f));
    renderer.strokeWidth(1.0f);
    for (int i = 0; i < 24; i++) {
        float a = (static_cast<float>(i) / 24.0f) * 2.0f * kPi;
        float x1 = x + cosf(a) * radius * 0.7f;
        float y1 = y + sinf(a) * radius * 0.7f;
        float x2 = x + cosf(a) * radius * 0.95f;
        float y2 = y + sinf(a) * radius * 0.95f;
        renderer.line(x1, y1, x2, y2);
    }
    
    // Center cap
    renderer.fillColor(Color(0.3f, 0.3f, 0.32f, 1.0f));
    renderer.circle(x, y, radius * 0.5f);
    renderer.fill();
    
    // Pointer
    float angle = valueToAngle(mAnimatedValue);
    float px = x + cosf(angle) * radius * 0.35f;
    float py = y + sinf(angle) * radius * 0.35f;
    
    renderer.fillColor(Color(0.9f, 0.9f, 0.8f, 1.0f));
    renderer.circle(px, py, radius * 0.1f);
    renderer.fill();
}

void Knob::renderModernKnob(WebGPURenderer& renderer, float x, float y, float size) {
    float radius = size * 0.4f;
    float normalizedValue = getValueNormalized();
    
    // Background arc (full range)
    renderer.strokeColor(Color(0.3f, 0.3f, 0.3f, 1.0f));
    renderer.strokeWidth(4.0f);
    renderer.beginPath();
    renderer.arc(x, y, radius, kStartAngle, kEndAngle, 0);
    renderer.stroke();
    
    // Value arc
    if (mBipolar) {
        float centerAngle = kStartAngle + kAngleRange * 0.5f;
        float valueAngle = valueToAngle(mAnimatedValue);
        
        renderer.strokeColor(mIndicatorColor);
        renderer.strokeWidth(4.0f);
        renderer.beginPath();
        if (normalizedValue >= 0.5f) {
            renderer.arc(x, y, radius, centerAngle, valueAngle, 0);
        } else {
            renderer.arc(x, y, radius, valueAngle, centerAngle, 0);
        }
        renderer.stroke();
    } else {
        float valueAngle = valueToAngle(mAnimatedValue);
        renderer.strokeColor(mIndicatorColor);
        renderer.strokeWidth(4.0f);
        renderer.beginPath();
        renderer.arc(x, y, radius, kStartAngle, valueAngle, 0);
        renderer.stroke();
    }
    
    // Center dot
    renderer.fillColor(mForegroundColor);
    renderer.circle(x, y, radius * 0.15f);
    renderer.fill();
}

void Knob::renderLEDKnob(WebGPURenderer& renderer, float x, float y, float size) {
    float radius = size * 0.4f;
    float normalizedValue = getValueNormalized();
    int numLEDs = 11;
    
    // Draw LED ring
    for (int i = 0; i < numLEDs; i++) {
        float ledPos = static_cast<float>(i) / (numLEDs - 1);
        float angle = kStartAngle + ledPos * kAngleRange;
        float ledX = x + cosf(angle) * radius;
        float ledY = y + sinf(angle) * radius;
        
        bool isLit = ledPos <= normalizedValue;
        
        // LED glow
        if (isLit) {
            renderer.fillColor(Color(mIndicatorColor.r, mIndicatorColor.g, mIndicatorColor.b, 0.3f));
            renderer.circle(ledX, ledY, radius * 0.12f);
            renderer.fill();
        }
        
        // LED body
        if (isLit) {
            renderer.fillColor(mIndicatorColor);
        } else {
            renderer.fillColor(Color(0.15f, 0.15f, 0.15f, 1.0f));
        }
        renderer.circle(ledX, ledY, radius * 0.08f);
        renderer.fill();
    }
    
    // Center knob
    renderer.fillColor(mBackgroundColor);
    renderer.circle(x, y, radius * 0.5f);
    renderer.fill();
    
    // Indicator
    float angle = valueToAngle(mAnimatedValue);
    float ix = x + cosf(angle) * radius * 0.35f;
    float iy = y + sinf(angle) * radius * 0.35f;
    renderer.fillColor(mForegroundColor);
    renderer.circle(ix, iy, radius * 0.08f);
    renderer.fill();
}

void Knob::renderMinimalKnob(WebGPURenderer& renderer, float x, float y, float size) {
    float radius = size * 0.4f;
    
    // Simple circle
    renderer.strokeColor(mForegroundColor);
    renderer.strokeWidth(2.0f);
    renderer.circle(x, y, radius);
    renderer.stroke();
    
    // Dot indicator
    float angle = valueToAngle(mAnimatedValue);
    float dotX = x + cosf(angle) * radius * 0.7f;
    float dotY = y + sinf(angle) * radius * 0.7f;
    
    renderer.fillColor(mIndicatorColor);
    renderer.circle(dotX, dotY, radius * 0.15f);
    renderer.fill();
}

bool Knob::hitTest(float mouseX, float mouseY, float knobX, float knobY, float size) const {
    float radius = size * 0.5f;
    float dx = mouseX - knobX;
    float dy = mouseY - knobY;
    return (dx * dx + dy * dy) <= (radius * radius);
}

void Knob::onMouseDown(float x, float y, float knobX, float knobY, float size) {
    mDragging = true;
    mDragStartValue = mValue;
    mDragStartY = y;
}

void Knob::onMouseDrag(float x, float y, float prevX, float prevY) {
    if (!mDragging) return;
    
    float sensitivity = mFineMode ? mSensitivity * 0.1f : mSensitivity;
    float deltaY = prevY - y;  // Inverted: drag up increases value
    
    float deltaValue = deltaY * sensitivity * (mMax - mMin);
    setValue(mValue + deltaValue);
}

void Knob::onMouseUp() {
    mDragging = false;
}

void Knob::onDoubleClick() {
    setValue(mDefaultValue);
}

void Knob::onScroll(float delta) {
    float sensitivity = mFineMode ? 0.01f : 0.05f;
    float deltaValue = delta * sensitivity * (mMax - mMin);
    setValue(mValue + deltaValue);
}

} // namespace wasm
} // namespace bespoke
