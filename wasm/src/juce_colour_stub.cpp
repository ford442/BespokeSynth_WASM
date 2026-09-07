/**
 * Minimal juce::Colour implementations so WASM can drop juce_graphics.cpp.
 * CORE TUs still include juce_gui_basics / juce_opengl headers that reference Colour.
 */

#include "juce_graphics/juce_graphics.h"

namespace juce
{
   Colour::Colour(uint32 col) noexcept
   : argb(static_cast<uint8>((col >> 24) & 0xff),
          static_cast<uint8>((col >> 16) & 0xff),
          static_cast<uint8>((col >> 8) & 0xff),
          static_cast<uint8>(col & 0xff))
   {
   }
} // namespace juce
