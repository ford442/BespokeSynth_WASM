# Example Debug Output

This file shows example console output when running BespokeSynth WASM with panel debugging enabled.

## Initialization Output

```
BespokeSynth WASM: Initializing (800x600, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)

=== DEBUG: Panel Initialization ===
DEBUG [Panel:Mixer] LOADED - Loaded:YES Running:NO Frames:0
DEBUG [Panel:Effects] LOADED - Loaded:YES Running:NO Frames:0
DEBUG [Panel:Sequencer] LOADED - Loaded:YES Running:NO Frames:0
=== Panel Initialization Complete ===

BespokeSynth WASM: Initialization complete
WasmBridge: notifying JS of init complete (0)
BespokeSynth WASM initialized
```

## First Render (Mixer Panel Starts Running)

```
DEBUG [Panel:Mixer] STARTED - Loaded:YES Running:YES Frames:1
```

## Switching Between Panels

### Switching from Mixer to Effects
```
DEBUG [Panel Switch] From:Mixer To:Effects
DEBUG [Panel:Effects] ACTIVATED - Loaded:YES Running:NO Frames:0
DEBUG [Panel:Effects] STARTED - Loaded:YES Running:YES Frames:1
```

### Switching from Effects to Sequencer
```
DEBUG [Panel Switch] From:Effects To:Sequencer
DEBUG [Panel:Sequencer] ACTIVATED - Loaded:YES Running:NO Frames:0
DEBUG [Panel:Sequencer] STARTED - Loaded:YES Running:YES Frames:1
```

## Periodic Status Updates

Running panels log status every 300 frames (~5 seconds at 60fps):

```
DEBUG [Panel:Effects] Running - Frames:300 Time:5.0s
DEBUG [Panel:Effects] Running - Frames:600 Time:10.0s
DEBUG [Panel:Effects] Running - Frames:900 Time:15.0s
DEBUG [Panel:Effects] Running - Frames:1200 Time:20.0s
```

## Manual Status Dump

When calling `Module._bespoke_log_all_panels_status()` or clicking the panel status widget:

```
=== DEBUG: All Panels Status ===
DEBUG [Panel:Mixer] STATUS CHECK - Loaded:YES Running:NO Frames:1234
DEBUG [Panel:Effects] STATUS CHECK - Loaded:YES Running:YES Frames:5678
DEBUG [Panel:Sequencer] STATUS CHECK - Loaded:YES Running:NO Frames:0
=== End Panel Status ===
```

## Panel State Examples

### All Panels Loaded, Mixer Active
```
=== DEBUG: All Panels Status ===
DEBUG [Panel:Mixer] STATUS CHECK - Loaded:YES Running:YES Frames:3456
DEBUG [Panel:Effects] STATUS CHECK - Loaded:YES Running:NO Frames:1234
DEBUG [Panel:Sequencer] STATUS CHECK - Loaded:YES Running:NO Frames:567
=== End Panel Status ===
```

### Effects Panel Just Activated
```
DEBUG [Panel Switch] From:Mixer To:Effects
DEBUG [Panel:Effects] ACTIVATED - Loaded:YES Running:NO Frames:1234
DEBUG [Panel:Effects] STARTED - Loaded:YES Running:YES Frames:1235
```

## Error Scenarios

### If WebGPU Fails to Initialize
```
BespokeSynth WASM: Initializing (800x600, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)
BespokeSynth WASM: Failed to initialize WebGPU
WasmBridge: notifying JS of init failure (-1)
```

In this case, panels will never be marked as loaded.

### If Renderer Fails
```
BespokeSynth WASM: Initializing (800x600, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)
BespokeSynth WASM: Failed to initialize renderer
WasmBridge: notifying JS of init failure (-2)
```

### If Audio Backend Fails
```
BespokeSynth WASM: Initializing (800x600, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)
BespokeSynth WASM: Failed to initialize audio
WasmBridge: notifying JS of init failure (-3)
```

## JavaScript Console Examples

### Querying Panel Status
```javascript
// Get panel count
var count = Module._bespoke_get_panel_count();
console.log('Panel count:', count);
// Output: Panel count: 3

// Get panel names
for (var i = 0; i < count; i++) {
    var name = Module.ccall('bespoke_get_panel_name', 'string', ['number'], [i]);
    console.log('Panel', i, 'name:', name);
}
// Output:
// Panel 0 name: Mixer
// Panel 1 name: Effects
// Panel 2 name: Sequencer

// Check panel status
for (var i = 0; i < count; i++) {
    var name = Module.ccall('bespoke_get_panel_name', 'string', ['number'], [i]);
    var loaded = Module._bespoke_is_panel_loaded(i);
    var running = Module._bespoke_is_panel_running(i);
    var frames = Module._bespoke_get_panel_frame_count(i);
    console.log('Panel:', name, 'Loaded:', loaded, 'Running:', running, 'Frames:', frames);
}
// Output:
// Panel: Mixer Loaded: 1 Running: 0 Frames: 1234
// Panel: Effects Loaded: 1 Running: 1 Frames: 5678
// Panel: Sequencer Loaded: 1 Running: 0 Frames: 567
```

### Monitoring Panel Activity
```javascript
// Monitor panel activity every second
setInterval(function() {
    var currentPanel = Module._bespoke_get_panel();
    var name = Module.ccall('bespoke_get_panel_name', 'string', ['number'], [currentPanel]);
    var frames = Module._bespoke_get_panel_frame_count(currentPanel);
    console.log('Active panel:', name, 'Frames:', frames);
}, 1000);
// Output (every second):
// Active panel: Effects Frames: 5678
// Active panel: Effects Frames: 5738
// Active panel: Effects Frames: 5798
```

## Visual Status Display

The panel status widget in the top-right corner shows:

```
┌─────────────────────────────┐
│ Panel Status                │
├─────────────────────────────┤
│ Mixer:      ● Loaded        │
│ Effects:    ✓ Running (5678 frames) │
│ Sequencer:  ● Loaded        │
└─────────────────────────────┘
```

Status indicators:
- `○ Not Loaded` - Gray, panel not initialized
- `● Loaded` - Blue, panel initialized but inactive
- `✓ Running (N frames)` - Green, panel is active and rendering

## Timeline Example

A typical session showing panel activity over time:

```
[00:00] App starts loading
[00:01] === DEBUG: Panel Initialization ===
[00:01] DEBUG [Panel:Mixer] LOADED - Loaded:YES Running:NO Frames:0
[00:01] DEBUG [Panel:Effects] LOADED - Loaded:YES Running:NO Frames:0
[00:01] DEBUG [Panel:Sequencer] LOADED - Loaded:YES Running:NO Frames:0
[00:01] === Panel Initialization Complete ===
[00:02] DEBUG [Panel:Mixer] STARTED - Loaded:YES Running:YES Frames:1
[00:07] DEBUG [Panel:Mixer] Running - Frames:300 Time:5.0s
[00:10] User clicks Effects tab
[00:10] DEBUG [Panel Switch] From:Mixer To:Effects
[00:10] DEBUG [Panel:Effects] ACTIVATED - Loaded:YES Running:NO Frames:0
[00:10] DEBUG [Panel:Effects] STARTED - Loaded:YES Running:YES Frames:1
[00:15] DEBUG [Panel:Effects] Running - Frames:300 Time:5.0s
[00:18] User clicks Sequencer tab
[00:18] DEBUG [Panel Switch] From:Effects To:Sequencer
[00:18] DEBUG [Panel:Sequencer] ACTIVATED - Loaded:YES Running:NO Frames:0
[00:18] DEBUG [Panel:Sequencer] STARTED - Loaded:YES Running:YES Frames:1
[00:23] DEBUG [Panel:Sequencer] Running - Frames:300 Time:5.0s
```

## Notes

- Frame counts are cumulative for each panel across its active periods
- Time values show elapsed time since app initialization
- All panels remain loaded once initialized
- Only the currently selected panel shows as "Running"
- Periodic status messages appear every 300 frames (~5 seconds at 60fps)
- Manual status dumps can be triggered anytime via JavaScript API
