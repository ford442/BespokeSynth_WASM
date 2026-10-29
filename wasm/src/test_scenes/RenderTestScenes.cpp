/**
 * BespokeSynth WASM - Render-test scenes (?renderTest=1 / =2), built with BESPOKE_WASM_RENDER_TESTS
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"

namespace bespoke
{
   namespace wasm
   {
      void ModuleCanvas::setupCanonicalRenderTestScene()
      {
         Lock lock(mMutex);
         UndoSuppressor suppress(mUndoSuppressDepth);
         mUndoStack.clear();
         clearUserModules();
         setViewTransform(0.0f, 80.0f, 1.0f);

         if (mTransport)
         {
            mTransport->setPosition(20.0f, 50.0f);
            mTransport->setBPM(128.0f);
            mTransport->setPlaying(false);
            mTransport->setControlValue("swing", 0.15f);
         }
         if (mScaleModule)
         {
            mScaleModule->setPosition(290.0f, 50.0f);
            mScaleModule->setControlValue("root", 0.0f);
            mScaleModule->setControlValue("type", 0.0f);
         }

         const int seqId = createModule("stepsequencer", 80.0f, 310.0f);
         const int oscId = createModule("oscillator", 280.0f, 160.0f);
         const int filterId = createModule("filter", 470.0f, 170.0f);
         const int gainId = createModule("gain", 650.0f, 180.0f);
         const int lfoId = createModule("lfo", 280.0f, 310.0f);
         const int outputId = createModule("output", 820.0f, 190.0f);

         if (auto* seq = getModule(seqId))
         {
            seq->setControlValue("pattern", static_cast<float>(0x1111));
            seq->setControlValue("pitch", 60.0f);
            seq->setControlValue("gate", 0.8f);
         }
         if (auto* osc = getModule(oscId))
         {
            osc->setControlValue("frequency", 440.0f);
            osc->setControlValue("volume", 0.75f);
            osc->setControlValue("waveform", 0.0f);
         }
         if (auto* filter = getModule(filterId))
         {
            filter->setControlValue("cutoff", 1800.0f);
            filter->setControlValue("resonance", 0.65f);
            filter->setControlValue("type", 0.0f);
         }
         if (auto* gain = getModule(gainId))
            gain->setControlValue("gain", 0.7f);
         if (auto* lfo = getModule(lfoId))
         {
            lfo->setControlValue("rate", 2.5f);
            lfo->setControlValue("depth", 0.6f);
            lfo->setControlValue("shape", 0.0f);
         }

         if (seqId > 0 && oscId > 0)
            connectModules(seqId, 0, oscId, 0);
         if (oscId > 0 && filterId > 0)
            connectModules(oscId, 0, filterId, 0);
         if (filterId > 0 && gainId > 0)
            connectModules(filterId, 0, gainId, 0);
         if (gainId > 0 && outputId > 0)
            connectModules(gainId, 0, outputId, 0);
         if (lfoId > 0 && filterId > 0)
            connectModules(lfoId, 0, filterId, 1);

         publishAudioGraphSnapshotLocked();
      }

      void ModuleCanvas::setupDrumRenderTestScene()
      {
         Lock lock(mMutex);
         UndoSuppressor suppress(mUndoSuppressDepth);
         mUndoStack.clear();
         clearUserModules();
         setViewTransform(0.0f, 80.0f, 1.0f);

         if (mTransport)
         {
            mTransport->setPosition(20.0f, 50.0f);
            mTransport->setBPM(120.0f);
            mTransport->setPlaying(false);
            mTransport->setControlValue("swing", 0.0f);
         }
         if (mScaleModule)
         {
            mScaleModule->setPosition(290.0f, 50.0f);
            mScaleModule->setControlValue("root", 0.0f);
            mScaleModule->setControlValue("type", 0.0f);
         }

         const int drumsId = createModule("drumseq", 20.0f, 170.0f);
         const int noiseId = createModule("noise", 20.0f, 340.0f);
         const int kickId = createModule("oscillator", 290.0f, 160.0f);
         const int snareId = createModule("adsr", 290.0f, 290.0f);
         const int hatId = createModule("adsr", 290.0f, 420.0f);
         const int mixerId = createModule("mixer", 500.0f, 190.0f);
         const int compId = createModule("compressor", 690.0f, 190.0f);
         const int outputId = createModule("output", 900.0f, 210.0f);

         if (auto* drums = getModule(drumsId))
         {
            drums->setControlValue("kick", static_cast<float>(0x1111)); // four on the floor
            drums->setControlValue("snare", static_cast<float>(0x1010));
            drums->setControlValue("hat", static_cast<float>(0x4444)); // off-beats
            drums->setControlValue("tom", 0.0f);
            drums->setControlValue("kickpitch", 33.0f);
            drums->setControlValue("gate", 0.4f);
         }
         if (auto* noise = getModule(noiseId))
            noise->setControlValue("volume", 0.6f);
         if (auto* kick = getModule(kickId))
         {
            kick->setControlValue("frequency", 55.0f);
            kick->setControlValue("volume", 0.9f);
            kick->setControlValue("waveform", 0.0f);
         }
         if (auto* snare = getModule(snareId))
         {
            snare->setControlValue("attack", 1.0f);
            snare->setControlValue("decay", 90.0f);
            snare->setControlValue("sustain", 0.0f);
            snare->setControlValue("release", 40.0f);
         }
         if (auto* hat = getModule(hatId))
         {
            hat->setControlValue("attack", 1.0f);
            hat->setControlValue("decay", 25.0f);
            hat->setControlValue("sustain", 0.0f);
            hat->setControlValue("release", 15.0f);
         }
         if (auto* mixer = getModule(mixerId))
         {
            mixer->setControlValue("level1", 0.9f);
            mixer->setControlValue("level2", 0.6f);
            mixer->setControlValue("level3", 0.35f);
            mixer->setControlValue("master", 0.9f);
         }
         if (auto* comp = getModule(compId))
         {
            comp->setControlValue("threshold", -18.0f);
            comp->setControlValue("ratio", 4.0f);
            comp->setControlValue("makeup", 3.0f);
         }

         if (drumsId > 0)
         {
            connectModules(drumsId, 0, kickId, 0); // Kick → oscillator Pitch
            connectModules(drumsId, 1, snareId, 1); // Snare → ADSR Gate
            connectModules(drumsId, 2, hatId, 1); // Hat → ADSR Gate
            if (mTransport)
               connectModules(mTransport->getId(), 0, drumsId, 0);
         }
         connectModules(noiseId, 0, snareId, 0);
         connectModules(noiseId, 0, hatId, 0);
         connectModules(kickId, 0, mixerId, 0);
         connectModules(snareId, 0, mixerId, 1);
         connectModules(hatId, 0, mixerId, 2);
         connectModules(mixerId, 0, compId, 0);
         connectModules(compId, 0, outputId, 0);

         publishAudioGraphSnapshotLocked();
      }

   } // namespace wasm
} // namespace bespoke
