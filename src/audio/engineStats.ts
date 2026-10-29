import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

/** Graph rebuild and control fast-path counters (bespoke_get_engine_stats_json). */
export interface EngineStats {
  /** Process plans compiled since startup. A knob drag adds none. */
  planCompiles: number;
  /** Topology snapshots the canvas published (spawn, delete, cables, patch load). */
  publishCount: number;
  /** Control changes sent to the engine as ParamChange instead of a publish. */
  paramChangesSent: number;
  generation: number;
  paramSeq: number;
  /** True while the AudioWorklet engine plays; the counters below are then the idle UI-module engine's. */
  remote: boolean;
  /** prepareRuntimeStates calls (one per publish while this engine plays). */
  prepareCount: number;
  adoptCount: number;
  paramChangesApplied: number;
  paramChangesDropped: number;
  paramChangesStale: number;
  skippedNodes: number;
  /** Bytes of resident sample buffers and looper arenas in the UI module. */
  residentBytes: number;
  /** Evict unused samples / arenas past this many bytes. */
  resourceBudget: number;
}

export function readEngineStats(module: BespokeSynthModule | null): EngineStats | null {
  if (!module) return null;
  const ptr = module._bespoke_get_engine_stats_json();
  if (!ptr) return null;
  try {
    return JSON.parse(module.UTF8ToString(ptr)) as EngineStats;
  } catch {
    return null;
  }
}
