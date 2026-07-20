export interface StoredPatch {
  id: string;
  name: string;
  json: string;
  updatedAt: number;
}

const DB_NAME = 'bespokesynth-wasm';
const STORE_NAME = 'patches';
const MAX_PATCH_BYTES = 5 * 1024 * 1024;

/** Shown in the saved-patches dropdown when IndexedDB has no entries. */
export const PATCH_EMPTY_PLACEHOLDER = 'No saved patches yet';

/** Default name for browser auto-save when the user does not choose one. */
export const PATCH_DEFAULT_AUTOSAVE_NAME = 'Auto-saved patch';

function validatePatchJson(json: string): void {
  if (!json || new Blob([json]).size > MAX_PATCH_BYTES) throw new Error('Patch exceeds the 5 MB limit');
  const data: unknown = JSON.parse(json);
  if (!data || typeof data !== 'object') throw new Error('Patch must contain a JSON object');
}

function openDatabase(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => request.result.createObjectStore(STORE_NAME, { keyPath: 'id' });
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('Unable to open patch storage'));
  });
}

function requestResult<T>(request: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error('Patch storage request failed'));
  });
}

export class PatchStorage {
  async list(): Promise<StoredPatch[]> {
    const db = await openDatabase();
    try {
      const patches = await requestResult(db.transaction(STORE_NAME).objectStore(STORE_NAME).getAll()) as StoredPatch[];
      return patches.sort((a, b) => b.updatedAt - a.updatedAt);
    } finally { db.close(); }
  }

  async save(name: string, json: string, id?: string): Promise<StoredPatch> {
    validatePatchJson(json);
    const patch: StoredPatch = { id: id ?? crypto.randomUUID(), name: name.trim() || 'Untitled patch', json, updatedAt: Date.now() };
    const db = await openDatabase();
    try { await requestResult(db.transaction(STORE_NAME, 'readwrite').objectStore(STORE_NAME).put(patch)); }
    finally { db.close(); }
    return patch;
  }

  async remove(id: string): Promise<void> {
    const db = await openDatabase();
    try { await requestResult(db.transaction(STORE_NAME, 'readwrite').objectStore(STORE_NAME).delete(id)); }
    finally { db.close(); }
  }
}

export { MAX_PATCH_BYTES, validatePatchJson };
