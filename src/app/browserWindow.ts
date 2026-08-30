import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface BespokeSynthFactoryConfig {
  canvas: HTMLCanvasElement | HTMLElement | null;
  print: (text: unknown) => void;
  printErr: (text: unknown) => void;
  locateFile?: (path: string, scriptDirectory: string) => string;
}

export type BespokeSynthFactory = (config: BespokeSynthFactoryConfig) => Promise<BespokeSynthModule>;

export interface BespokeBrowserWindow extends Window {
  createBespokeSynth?: BespokeSynthFactory;
  Module?: BespokeSynthModule;
  __bespoke_on_init_progress?: (step: string, detail: string) => void;
  __bespoke_on_init_complete?: (status: number) => void;
  __bespoke_on_device_lost?: () => void;
}

export const bespokeWindow = window as BespokeBrowserWindow;
