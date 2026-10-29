import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import type { ScriptEngineLink } from '../audio/engineShared';
import type { LiveInstrument } from '../app/liveInstrument';
import { readCString, readJson, withCStrings } from '../app/wasmStrings';
import { ScriptHost, type ScriptLogEntry, type ScriptStatus } from '../script/scriptHost';
import { DEFAULT_SCRIPT } from '../script/scriptSandbox';

export function isScriptingRequested(search: string = window.location.search): boolean {
  const value = new URLSearchParams(search).get('script');
  return value === '1' || value === 'true';
}

const MAX_LOG_LINES = 200;

export interface ScriptPanelHandle {
  /** Call once the WASM module is initialized. */
  attach(module: BespokeSynthModule): void;
  run(code?: string, moduleId?: number): number;
  stop(): void;
  status(): ScriptStatus;
  log(): ScriptLogEntry[];
  open(): void;
}

/**
 * Side panel for the Script module (?script=1 only). Code runs only after an
 * explicit Run click/API call: scripts restored from a patch or share URL are
 * shown, never executed automatically.
 */
export function setupScriptPanel(
  headerControls: Element,
  getModule: () => BespokeSynthModule | null,
  live?: LiveInstrument,
  /** The AudioWorklet engine while it plays: running scripts send it notes directly. */
  getEngineLink: () => ScriptEngineLink | null = () => null,
): ScriptPanelHandle | null {
  if (!isScriptingRequested()) return null;

  const hosts = new Map<number, ScriptHost>();
  const logLines: ScriptLogEntry[] = [];
  let activeHost: ScriptHost | null = null;
  let targetId = -1;

  const toggle = document.createElement('button');
  toggle.id = 'scriptBtn';
  toggle.className = 'btn';
  toggle.textContent = 'Script';
  toggle.title = 'JavaScript livecoding (sandboxed worker)';
  headerControls.appendChild(toggle);

  const panel = document.createElement('aside');
  panel.id = 'scriptPanel';
  panel.className = 'script-panel';
  panel.hidden = true;
  panel.setAttribute('aria-label', 'Script editor');
  panel.innerHTML = `
    <div class="script-panel-header">
      <strong>Script</strong>
      <select id="scriptTargetSelect" class="renderer-select" title="Script module"></select>
      <span id="scriptStatus" class="script-status">stopped</span>
      <button type="button" id="scriptCloseBtn" class="btn script-close" aria-label="Close script panel">×</button>
    </div>
    <textarea id="scriptEditor" class="script-editor" spellcheck="false" autocapitalize="off" autocomplete="off" wrap="off"></textarea>
    <div class="script-panel-actions">
      <button type="button" id="scriptRunBtn" class="btn btn-play">Run</button>
      <button type="button" id="scriptStopBtn" class="btn btn-stop">Stop</button>
      <span class="script-note">No network, DOM, or WASM heap. Worker is restarted if it hangs.</span>
    </div>
    <pre id="scriptLog" class="script-log" aria-live="polite"></pre>
  `;
  document.getElementById('canvas-container')?.appendChild(panel);

  const editor = panel.querySelector<HTMLTextAreaElement>('#scriptEditor');
  const targetSelect = panel.querySelector<HTMLSelectElement>('#scriptTargetSelect');
  const statusEl = panel.querySelector<HTMLElement>('#scriptStatus');
  const logEl = panel.querySelector<HTMLElement>('#scriptLog');
  if (!editor || !targetSelect || !statusEl || !logEl) return null;

  const appendLog = (entry: ScriptLogEntry) => {
    logLines.push(entry);
    if (logLines.length > MAX_LOG_LINES) logLines.shift();
    const line = document.createElement('div');
    line.className = `script-log-${entry.level}`;
    line.textContent = entry.text;
    logEl.appendChild(line);
    while (logEl.childElementCount > MAX_LOG_LINES) logEl.firstElementChild?.remove();
    logEl.scrollTop = logEl.scrollHeight;
  };

  const showStatus = (status: ScriptStatus) => {
    statusEl.textContent = status;
    statusEl.dataset.status = status;
  };

  const scriptModules = (): number[] => {
    const module = getModule();
    if (!module) return [];
    const list = readJson<Array<{ id: number; type: string }>>(module, module._bespoke_get_modules_json(), []);
    return list.filter((m) => m.type === 'script').map((m) => m.id).sort((a, b) => a - b);
  };

  const loadSource = (id: number) => {
    const module = getModule();
    if (!module || id < 0) {
      editor.value = DEFAULT_SCRIPT;
      return;
    }
    const text = withCStrings(module, ['script'], (key) => readCString(module, module._bespoke_get_module_string(id, key)));
    editor.value = text || DEFAULT_SCRIPT;
  };

  const refreshTargets = () => {
    const ids = scriptModules();
    targetSelect.replaceChildren(
      ...ids.map((id) => new Option(`Script #${id}`, String(id))),
      new Option('+ New script module', '-1'),
    );
    if (!ids.includes(targetId)) {
      targetId = ids[0] ?? -1;
      loadSource(targetId);
    }
    targetSelect.value = String(targetId);
    showStatus(hosts.get(targetId)?.getStatus() ?? 'stopped');
  };

  const ensureTarget = (): number => {
    const module = getModule();
    if (!module) return -1;
    if (targetId >= 0 && scriptModules().includes(targetId)) return targetId;
    const created = module.ccall?.('bespoke_create_module', 'number', ['string', 'number', 'number'], ['script', 40, 470]);
    targetId = typeof created === 'number' ? created : -1;
    refreshTargets();
    return targetId;
  };

  const hostFor = (id: number): ScriptHost | null => {
    const module = getModule();
    if (!module || id < 0) return null;
    let host = hosts.get(id);
    if (!host) {
      host = new ScriptHost(
        module,
        id,
        {
          onLog: appendLog,
          onStatus: (status) => {
            if (id === targetId) showStatus(status);
          },
          onNote: (pitch, velocity, on, route) => {
            // Cabled script notes are reported by the graph; only keyboard-style ones are echoed here.
            if (route === 2) live?.echoPerformanceNote(on, 0, pitch, velocity);
          },
        },
        getEngineLink,
      );
      hosts.set(id, host);
    }
    return host;
  };

  const run = (code?: string, moduleId?: number): number => {
    if (moduleId !== undefined && moduleId >= 0) targetId = moduleId;
    const id = ensureTarget();
    const module = getModule();
    if (!module || id < 0) return -1;
    const source = code ?? editor.value;
    if (code !== undefined) editor.value = code;
    // Persist source with the patch (extras.script) before running it.
    withCStrings(module, ['script', source], (key, value) => module._bespoke_set_module_string(id, key, value));
    activeHost = hostFor(id);
    appendLog({ level: 'system', text: `▶ running Script #${id}` });
    activeHost?.run(source);
    return id;
  };

  const stop = () => {
    hosts.get(targetId)?.stop();
  };

  const open = () => {
    panel.hidden = false;
    toggle.classList.add('active');
    refreshTargets();
    editor.focus();
  };

  toggle.addEventListener('click', () => {
    if (panel.hidden) open();
    else {
      panel.hidden = true;
      toggle.classList.remove('active');
    }
  });
  panel.querySelector('#scriptCloseBtn')?.addEventListener('click', () => {
    panel.hidden = true;
    toggle.classList.remove('active');
  });
  panel.querySelector('#scriptRunBtn')?.addEventListener('click', () => run());
  panel.querySelector('#scriptStopBtn')?.addEventListener('click', stop);
  targetSelect.addEventListener('change', () => {
    targetId = Number(targetSelect.value);
    loadSource(targetId);
    showStatus(hosts.get(targetId)?.getStatus() ?? 'stopped');
  });
  editor.addEventListener('keydown', (event) => {
    // Ctrl/Cmd+Enter runs; Tab indents instead of leaving the editor.
    if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') {
      event.preventDefault();
      run();
    } else if (event.key === 'Tab') {
      event.preventDefault();
      editor.setRangeText('  ', editor.selectionStart, editor.selectionEnd, 'end');
    }
  });
  editor.value = DEFAULT_SCRIPT;

  return {
    attach(module) {
      module._bespoke_set_scripting_enabled(1);
    },
    run,
    stop,
    status: () => hosts.get(targetId)?.getStatus() ?? 'stopped',
    log: () => logLines.map((entry) => ({ ...entry })),
    open,
  };
}
