import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import {
  bespokeWindow,
  type BespokeSynthFactoryConfig,
} from './browserWindow';

export const WASM_SCRIPT_PATH = 'wasm/BespokeSynthWASM.js';

/** Resolve Emscripten companion assets (.wasm, .data) next to the injected script. */
export const resolveWasmAssetUrl = (scriptElement: HTMLScriptElement, file: string): string =>
  new URL(file, new URL(scriptElement.src, document.baseURI)).href;

/** Load WASM module script dynamically. */
export const loadWasmModule = async (canvas?: HTMLCanvasElement): Promise<BespokeSynthModule> => {
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = WASM_SCRIPT_PATH;
    script.onload = async () => {
      console.log('loadWasmModule: script loaded');
      const locateWasmAsset = (path: string) => resolveWasmAssetUrl(script, path);
      const factory = bespokeWindow.createBespokeSynth;
      console.log('loadWasmModule: factory type =', typeof factory);
      // MODULARIZE build: the glue defines only the factory (EXPORT_NAME=createBespokeSynth).
      if (typeof factory !== 'function') {
        reject(new Error(`${WASM_SCRIPT_PATH} did not define createBespokeSynth`));
        return;
      }
      const config: BespokeSynthFactoryConfig = {
        canvas: canvas ?? document.getElementById('canvas'),
        print: (text: unknown) => console.log(text),
        printErr: (text: unknown) => console.error(text),
        locateFile: locateWasmAsset,
      };

      try {
        console.log('loadWasmModule: invoking factory to create module instance');
        const instance = await factory(config);
        console.log('loadWasmModule: factory resolved an instance');
        resolve(instance);
      } catch (err) {
        console.error('loadWasmModule: factory threw error', err);
        reject(err);
      }
    };
    script.onerror = () => reject(new Error('Failed to load WASM module'));
    document.head.appendChild(script);
  });
};
