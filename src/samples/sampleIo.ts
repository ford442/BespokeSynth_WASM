import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { getSampleBlob, putSampleBlob, sha256Hex } from './opfsStore';

function writeHeapBytes(module: BespokeSynthModule, bytes: Uint8Array): number {
  const ptr = module._malloc(bytes.byteLength);
  module.HEAPU8.set(bytes, ptr);
  return ptr;
}

export function loadSampleIntoWasm(
  module: BespokeSynthModule,
  bytes: Uint8Array,
  name: string,
): { sampleId: number; hash: string } {
  const dataPtr = writeHeapBytes(module, bytes);
  const namePtr = module.allocateUTF8(name);
  try {
    const sampleId = module._bespoke_load_sample(dataPtr, bytes.byteLength, namePtr);
    const hashPtr = module._bespoke_get_sample_hash(sampleId);
    const hash = hashPtr ? module.UTF8ToString(hashPtr) : '';
    return { sampleId, hash };
  } finally {
    module._free(dataPtr);
    module._free(namePtr);
  }
}

export function assignSampleToModule(module: BespokeSynthModule, moduleId: number, hash: string): boolean {
  const hashPtr = module.allocateUTF8(hash);
  try {
    return module._bespoke_assign_sample(moduleId, hashPtr) === 1;
  } finally {
    module._free(hashPtr);
  }
}

export function findOrCreateSampler(module: BespokeSynthModule, x = 220, y = 220): number {
  const typePtr = module.allocateUTF8('sampler');
  try {
    const existing = module._bespoke_find_first_module(typePtr);
    if (existing >= 0) return existing;
    if (!module.ccall) return -1;
    const created = module.ccall('bespoke_create_module', 'number', ['string', 'number', 'number'], ['sampler', x, y]);
    return typeof created === 'number' ? created : -1;
  } finally {
    module._free(typePtr);
  }
}

export async function importAudioFile(module: BespokeSynthModule, file: File): Promise<number> {
  const buffer = await file.arrayBuffer();
  const bytes = new Uint8Array(buffer);
  const hash = await sha256Hex(buffer);
  await putSampleBlob(hash, buffer, file.name);
  const loaded = loadSampleIntoWasm(module, bytes, file.name);
  const samplerId = findOrCreateSampler(module);
  if (samplerId >= 0 && loaded.hash) {
    assignSampleToModule(module, samplerId, loaded.hash);
  }
  return loaded.sampleId;
}

export async function restorePatchSamples(module: BespokeSynthModule, hashes: string[]): Promise<void> {
  for (const hash of hashes) {
    const stored = await getSampleBlob(hash);
    if (!stored) continue;
    loadSampleIntoWasm(module, new Uint8Array(stored.bytes), stored.name);
  }
}

export function installSampleDropTarget(
  canvas: HTMLCanvasElement,
  getModule: () => BespokeSynthModule | null,
  onStatus?: (message: string) => void,
): void {
  const onDragOver = (event: DragEvent) => {
    if (!event.dataTransfer?.types.includes('Files')) return;
    event.preventDefault();
    canvas.classList.add('sample-drop-active');
  };
  const onDragLeave = () => canvas.classList.remove('sample-drop-active');
  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    canvas.classList.remove('sample-drop-active');
    const module = getModule();
    const file = event.dataTransfer?.files?.[0];
    if (!module || !file) return;
    void importAudioFile(module, file)
      .then((id) => onStatus?.(id >= 0 ? `Loaded ${file.name}` : `Could not decode ${file.name}`))
      .catch((error: unknown) => {
        console.error('Sample import failed', error);
        onStatus?.('Sample import failed');
      });
  };
  canvas.addEventListener('dragover', onDragOver);
  canvas.addEventListener('dragleave', onDragLeave);
  canvas.addEventListener('drop', onDrop);
}

export function promptForAudioFile(module: BespokeSynthModule): Promise<number> {
  return new Promise((resolve) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'audio/wav,audio/flac,audio/mpeg,audio/mp3,.wav,.flac,.mp3';
    input.addEventListener('change', () => {
      const file = input.files?.[0];
      if (!file) {
        resolve(-1);
        return;
      }
      void importAudioFile(module, file).then(resolve).catch(() => resolve(-1));
    });
    input.click();
  });
}
