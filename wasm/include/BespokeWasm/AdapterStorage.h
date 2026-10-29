/**
 * BespokeSynth WASM - Typed parameter / runtime-state storage for module adapters
 *
 * The engine hands adapters raw arena bytes (params in the snapshot, runtime state in
 * the engine's arena). Deriving from AdapterStorage<TParams, TState> generates the sizes,
 * placement-new and destruction, and gives typed access, so an adapter never casts or
 * constructs those blocks by hand.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/WasmModuleAdapter.h"
#include <new>
#include <type_traits>

namespace bespoke
{
   namespace wasm
   {

      /** TParams / TState for an adapter that has none. */
      struct NoAdapterStorage
      {
      };

      template <typename TParams, typename TState = NoAdapterStorage>
      class AdapterStorage : public WasmModuleAdapter
      {
         // Params cross the wire byte for byte (AudioGraphSnapshotCodec) and descriptors address them by offsetof.
         static_assert(std::is_trivially_copyable_v<TParams>, "Params blocks must be trivially copyable");
         static_assert(std::is_standard_layout_v<TParams>, "Params blocks must be standard layout (offsetof)");

      public:
         using Params = TParams;
         using State = TState;

         static constexpr bool kHasParams = !std::is_empty_v<TParams>;
         static constexpr bool kHasState = !std::is_empty_v<TState>;

         size_t paramsSize() const final { return kHasParams ? sizeof(TParams) : 0; }
         void initParams(void* params) const final
         {
            if constexpr (kHasParams)
               new (params) TParams();
         }

         size_t runtimeStateSize() const final { return kHasState ? sizeof(TState) : 0; }
         /** Default-constructs TState; an adapter that seeds more may override and call this first. */
         void initRuntimeState(void* runtimeState) const override
         {
            if constexpr (kHasState)
               new (runtimeState) TState();
         }
         void destroyRuntimeState(void* runtimeState) const override
         {
            if constexpr (kHasState)
               static_cast<TState*>(runtimeState)->~TState();
         }

      protected:
         static const TParams& paramsOf(const void* params) { return *static_cast<const TParams*>(params); }
         static TParams& paramsOf(void* params) { return *static_cast<TParams*>(params); }
         static TState& stateOf(void* runtimeState) { return *static_cast<TState*>(runtimeState); }
      };

   } // namespace wasm
} // namespace bespoke
