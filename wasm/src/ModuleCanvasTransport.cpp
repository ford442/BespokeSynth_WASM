/**
 * BespokeSynth WASM - Canvas transport (play state and tempo)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"

namespace bespoke
{
   namespace wasm
   {
      bool ModuleCanvas::isTransportPlaying() const
      {
         Lock lock(mMutex);
         return mTransport && mTransport->isPlaying();
      }

      void ModuleCanvas::setTransportPlaying(bool playing)
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setPlaying(playing);
         syncTransportLocked();
      }

      void ModuleCanvas::toggleTransportPlaying()
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setPlaying(!mTransport->isPlaying());
         syncTransportLocked();
      }

      float ModuleCanvas::getTransportBPM() const
      {
         Lock lock(mMutex);
         return mTransport ? mTransport->getBPM() : 120.0f;
      }

      void ModuleCanvas::setTransportBPM(float bpm)
      {
         Lock lock(mMutex);
         if (mTransport)
            mTransport->setBPM(bpm);
         syncTransportLocked();
      }

   } // namespace wasm
} // namespace bespoke
