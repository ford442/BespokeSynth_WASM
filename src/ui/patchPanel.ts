import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { getPatchStateJson, loadPatchStateJson } from '../patchState';
import {
  PatchStorage,
  PATCH_DEFAULT_AUTOSAVE_NAME,
  PATCH_EMPTY_PLACEHOLDER,
  type StoredPatch,
} from '../patchStorage';

export interface PatchPanelBindings {
  refreshPatchList: () => Promise<StoredPatch[]>;
  showPatchStatus: (message: string, durationMs?: number) => void;
  configureAutoSave: (enabled: boolean) => void;
}

export function setupPatchPanel(
  controls: Element,
  getModule: () => BespokeSynthModule | null,
  storage: PatchStorage,
  callbacks: {
    getSelectedPatchId: () => string | undefined;
    setSelectedPatchId: (id: string | undefined) => void;
    getLastAutoSaveJson: () => string;
    setLastAutoSaveJson: (json: string) => void;
    configureAutoSave: (enabled: boolean) => void;
    showPatchStatus: (message: string, durationMs?: number) => void;
  },
): PatchPanelBindings {
  const select = document.createElement('select');
  select.id = 'patchSelect';
  select.className = 'renderer-select';
  select.title = 'Saved browser patches';
  const save = document.createElement('button');
  save.className = 'btn';
  save.textContent = 'Save browser';
  const load = document.createElement('button');
  load.className = 'btn';
  load.textContent = 'Load browser';
  const rename = document.createElement('button');
  rename.className = 'btn';
  rename.textContent = 'Rename';
  const remove = document.createElement('button');
  remove.className = 'btn';
  remove.textContent = 'Delete';
  const auto = document.createElement('label');
  auto.className = 'autosave-toggle';
  const checkbox = document.createElement('input');
  checkbox.type = 'checkbox';
  checkbox.id = 'autosaveToggle';
  auto.append(checkbox, document.createTextNode(' Auto-save'));

  const refresh = async (): Promise<StoredPatch[]> => {
    const patches = await storage.list();
    select.replaceChildren();
    if (patches.length === 0) {
      const placeholder = new Option(PATCH_EMPTY_PLACEHOLDER, '');
      placeholder.disabled = true;
      placeholder.selected = true;
      select.add(placeholder);
      select.value = '';
    } else {
      for (const patch of patches) select.add(new Option(patch.name, patch.id));
      const selectedId =
        callbacks.getSelectedPatchId() &&
        patches.some((patch) => patch.id === callbacks.getSelectedPatchId())
          ? callbacks.getSelectedPatchId()
          : '';
      select.value = selectedId ?? '';
    }
    return patches;
  };

  save.addEventListener('click', async () => {
    const defaultName = select.value
      ? select.selectedOptions[0]?.textContent ?? 'Untitled patch'
      : 'Untitled patch';
    const name = window.prompt('Patch name', defaultName);
    if (name === null) return;
    try {
      const patch = await storage.save(name, getPatchStateJson(getModule()), callbacks.getSelectedPatchId());
      callbacks.setSelectedPatchId(patch.id);
      callbacks.setLastAutoSaveJson(patch.json);
      await refresh();
      callbacks.showPatchStatus(`Saved "${patch.name}"`);
    } catch (error) {
      console.error('Patch save failed:', error);
      callbacks.showPatchStatus('Save failed — patch may be too large or invalid');
    }
  });

  load.addEventListener('click', async () => {
    const patch = (await storage.list()).find((item) => item.id === select.value);
    if (!patch) {
      callbacks.showPatchStatus('Select a saved patch to load');
      return;
    }
    if (loadPatchStateJson(getModule(), patch.json)) {
      callbacks.setSelectedPatchId(patch.id);
      callbacks.setLastAutoSaveJson(patch.json);
      callbacks.showPatchStatus(`Loaded "${patch.name}"`);
    } else {
      callbacks.showPatchStatus('Failed to load patch');
    }
  });

  rename.addEventListener('click', async () => {
    const patch = (await storage.list()).find((item) => item.id === select.value);
    if (!patch) {
      callbacks.showPatchStatus('Select a saved patch to rename');
      return;
    }
    const name = window.prompt('Patch name', patch.name);
    if (name === null) return;
    const renamed = await storage.save(name, patch.json, patch.id);
    await refresh();
    callbacks.showPatchStatus(`Renamed to "${renamed.name}"`);
  });

  remove.addEventListener('click', async () => {
    if (!select.value) {
      callbacks.showPatchStatus('Select a saved patch to delete');
      return;
    }
    const patchName = select.selectedOptions[0]?.textContent ?? 'patch';
    await storage.remove(select.value);
    callbacks.setSelectedPatchId(undefined);
    await refresh();
    callbacks.showPatchStatus(`Deleted "${patchName}"`);
  });

  checkbox.addEventListener('change', () => {
    callbacks.configureAutoSave(checkbox.checked);
    if (checkbox.checked) {
      callbacks.showPatchStatus('Auto-save enabled');
    }
  });

  controls.append(select, save, load, rename, remove, auto);
  void refresh();

  return {
    refreshPatchList: refresh,
    showPatchStatus: callbacks.showPatchStatus,
    configureAutoSave: callbacks.configureAutoSave,
  };
}

export function createPatchStatusHelpers(): {
  showPatchStatus: (message: string, durationMs?: number) => void;
  patchStatusTimerRef: { current: number | null };
} {
  const patchStatusTimerRef: { current: number | null } = { current: null };

  const showPatchStatus = (message: string, durationMs = 3200): void => {
    let toast = document.getElementById('patch-status-toast');
    if (!toast) {
      toast = document.createElement('div');
      toast.id = 'patch-status-toast';
      toast.className = 'patch-status-toast';
      toast.setAttribute('role', 'status');
      toast.setAttribute('aria-live', 'polite');
      document.body.appendChild(toast);
    }
    toast.textContent = message;
    toast.classList.add('visible');
    if (patchStatusTimerRef.current !== null) window.clearTimeout(patchStatusTimerRef.current);
    patchStatusTimerRef.current = window.setTimeout(() => {
      toast?.classList.remove('visible');
      patchStatusTimerRef.current = null;
    }, durationMs);
  };

  return { showPatchStatus, patchStatusTimerRef };
}

export { PATCH_DEFAULT_AUTOSAVE_NAME };
