/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2021 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/
//
//  VSTPlayhead.cpp
//  Bespoke
//
//  Created by Ryan Challinor on 1/20/16.
//
//

#include "VSTPlayhead.h"
#include "Transport.h"

std::optional<juce::AudioPlayHead::PositionInfo> VSTPlayhead::getPosition() const
{
   juce::AudioPlayHead::PositionInfo pos;

   // Fill basic fields from Transport
   pos.bpm = TheTransport->GetTempo();
   pos.timeSigNumerator = TheTransport->GetTimeSigTop();
   pos.timeSigDenominator = TheTransport->GetTimeSigBottom();

   pos.timeInSamples = static_cast<int64_t>(gTime * gSampleRateMs);
   pos.timeInSeconds = gTime / 1000.0;

   // Compute ppq positions similar to the original logic
   double tsRatio = 4.0;
   if (pos.timeSigNumerator > 0)
      tsRatio = 1.0 * pos.timeSigNumerator / pos.timeSigDenominator * 4.0;

   pos.ppqPosition = (TheTransport->GetMeasureTime(gTime)) * tsRatio;
   pos.ppqPositionOfLastBarStart = floor(TheTransport->GetMeasureTime(gTime)) * tsRatio;

   // Loop points (best-effort mapping)
   pos.ppqLoopStart = 0;
   pos.ppqLoopEnd = 480 * pos.timeSigDenominator; // approximate equivalent to original behavior

   pos.isPlaying = true;
   pos.isRecording = false;
   pos.isLooping = false;

   pos.frameRate = juce::AudioPlayHead::FrameRate(juce::AudioPlayHead::fps60);

   return pos;
}
