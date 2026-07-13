# ADR: Python livecoding in the WASM port

Status: accepted for the current WASM milestone. This is an investigation only; it does not add a scripting runtime.

## Decision

Keep Python livecoding explicitly out of scope for the current WASM port. Re-evaluate a worker-hosted Pyodide prototype only after the audio graph and module-port contracts are stable. If browser scripting is later funded as a smaller, WASM-native feature, prefer a capability-limited JavaScript module over `Function`/`eval` on the main thread.

## Options considered

| Option | Result | Why |
|---|---|---|
| Pyodide in a dedicated worker | Deferred | Best Python compatibility, but a large lazy download and a message-based control API are required. |
| JavaScript scripting module | Deferred alternative | Smaller and browser-native, but incompatible with existing Python scripts. |
| No browser scripting | Chosen now | Avoids freezing a control/scheduling API before #63 and #71 establish it. |

## Size estimate

Estimate the **lazy** Pyodide worker download at roughly **10–20 MiB compressed** for the core runtime, standard library, loader, and a selected scientific package such as NumPy; allow **20–35 MiB transferred** as a conservative first-use budget until a pinned-version build measurement is recorded. Do not ship the full distribution: upstream describes it as 200+ MB and recommends selecting exact package wheels. The final design must report compressed and uncompressed artifact sizes for the pinned Pyodide release.

## Desktop API surface

`Source/ScriptModule_PythonInterface.i` exposes substantially more than a generic evaluator. A feasible first browser API is intentionally smaller:

| Tier | Required API |
|---|---|
| Transport and scale reads | measure/time/subdivision, tempo, root, scale, pitch conversion |
| Module control | enumerate supported modules/controls; get/set/adjust a numeric control |
| Event output | queue note on/off and bounded scheduled control changes |
| Diagnostics | structured console output and line/error location |

Defer desktop-only bindings: arbitrary module references, OSC, SysEx, controller state, VST, file/sample manipulation, snapshots, UI text mutation, and direct Python access to browser objects. Each script action must become a validated message to the main thread, then an audio-safe graph event—not a direct mutation of the audio callback state.

## Security review

The worker boundary is isolation, not a complete permission system. The script runtime must:

- receive a frozen, capability-based `bespoke` API; no raw `self`, `window`, Emscripten `FS`, or WASM memory handles;
- deny filesystem persistence by default; enable an app-private IndexedDB store only behind an explicit permission;
- deny network by default: do not expose `fetch`, sockets, `pyodide.http`, `micropip`, or dynamic package installation;
- enforce per-run wall-time and message-size limits, terminate/recreate a wedged worker, and bound scheduled-event queues;
- treat loaded patches/scripts as untrusted and require an explicit user action before executing them.

Pyodide can bridge JavaScript objects into Python, and browser code has access to Web APIs if exposed; therefore the host must register only the narrow API above. A worker also prevents long-running script execution from blocking UI, but does not make untrusted code safe by itself.

## Revisit gate

Do not prototype until #63 delivers a stable scheduled-event boundary and #71 defines the supported module/control registry. Then build a feature-flagged worker POC with no filesystem/network capability, one oscillator-control example, and measured startup/download/CPU impact before choosing Pyodide or JavaScript.

## Sources

- [Pyodide deployment and package-selection guidance](https://pyodide.org/en/stable/usage/downloading-and-deploying.html)
- [Pyodide bundler guidance](https://pyodide.org/en/stable/usage/working-with-bundlers.html)
- [Pyodide FAQ: JavaScript modules and browser filesystem constraints](https://pyodide.org/en/stable/usage/faq.html)
