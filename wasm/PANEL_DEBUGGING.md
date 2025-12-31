# Panel Debugging Features

This document describes the debugging features added to track panel loading and running status in the BespokeSynth WASM version.

## Overview

The WASM version of BespokeSynth includes three main panels:
1. **Mixer** - Audio level controls and VU meters
2. **Effects** - Effect parameters and visualization
3. **Sequencer** - Step sequencer and pattern controls

These debugging features help developers and users understand if panels are properly loaded and running.

## Debug Logging in Console

### Panel Initialization
When the synth initializes, you'll see debug messages like:
```
=== DEBUG: Panel Initialization ===
DEBUG [Panel:Mixer] LOADED - Loaded:YES Running:NO Frames:0
DEBUG [Panel:Effects] LOADED - Loaded:YES Running:NO Frames:0
DEBUG [Panel:Sequencer] LOADED - Loaded:YES Running:NO Frames:0
=== Panel Initialization Complete ===
```

### Panel Activation
When switching between panels:
```
DEBUG [Panel Switch] From:Mixer To:Effects
DEBUG [Panel:Effects] ACTIVATED - Loaded:YES Running:NO Frames:0
```

### Panel Running Status
When a panel starts running (first render):
```
DEBUG [Panel:Effects] STARTED - Loaded:YES Running:YES Frames:1
```

### Periodic Status Updates
Every 300 frames (~5 seconds at 60fps), the currently active panel logs:
```
DEBUG [Panel:Effects] Running - Frames:300 Time:5.0s
DEBUG [Panel:Effects] Running - Frames:600 Time:10.0s
```

## Visual Panel Status Display

A panel status widget appears in the top-right corner of the UI showing:
- Panel name
- Loading status (blue dot)
- Running status (green checkmark)
- Frame count for running panels

Example display:
```
Panel Status
━━━━━━━━━━━━━━━━━━━━━━
Mixer:      ✓ Running (1234 frames)
Effects:    ● Loaded
Sequencer:  ● Loaded
```

### Status Indicators
- `○ Not Loaded` - Panel not yet initialized (gray)
- `● Loaded` - Panel initialized but not currently active (blue)
- `✓ Running (N frames)` - Panel is currently active and rendering (green)

### Interactive Features
Click on the panel status display to dump complete status information to the console.

## JavaScript API

New functions exposed to JavaScript for querying panel status:

### Get Panel Count
```javascript
var count = Module._bespoke_get_panel_count();
// Returns: 3 (Mixer, Effects, Sequencer)
```

### Get Panel Name
```javascript
var name = Module.ccall('bespoke_get_panel_name', 'string', ['number'], [0]);
// Returns: "Mixer" for index 0
```

### Check if Panel is Loaded
```javascript
var isLoaded = Module._bespoke_is_panel_loaded(0);
// Returns: 1 if loaded, 0 if not
```

### Check if Panel is Running
```javascript
var isRunning = Module._bespoke_is_panel_running(0);
// Returns: 1 if currently rendering, 0 if not
```

### Get Panel Frame Count
```javascript
var frames = Module._bespoke_get_panel_frame_count(0);
// Returns: number of frames rendered by this panel
```

### Log All Panel Status
```javascript
Module._bespoke_log_all_panels_status();
// Outputs detailed status for all panels to console
```

## Visual Indicators

### Panel Tab Border Colors
Panel tabs in the UI now use colored borders to indicate status:
- **Green** - Panel is loaded AND currently running
- **Blue** - Panel is currently selected/active
- **Gray** - Panel is inactive

This provides immediate visual feedback about which panels are functioning.

## Code Changes Summary

### WasmBridge.cpp Changes

1. **Added PanelStatus struct** - Tracks loaded/running state, frame count, and last update time
2. **Added helper functions**:
   - `getPanelName()` - Returns panel name string
   - `logPanelStatus()` - Logs formatted panel status to console
   - `markPanelLoaded()` - Marks a panel as loaded during initialization
   - `markPanelRunning()` - Marks a panel as running when first rendered

3. **Enhanced bespoke_init()** - Logs all panels as loaded after initialization
4. **Enhanced bespoke_render()** - Tracks active panel, updates frame counts, periodic logging
5. **Enhanced panel switching** - Logs when switching between panels
6. **Added new API functions** - Expose panel status to JavaScript

### shell.html Changes

1. **Added CSS styles** for panel status display widget
2. **Added panel-status div** - Container for status display
3. **Added updatePanelStatus()** - JavaScript function to query and display panel status
4. **Enhanced status update loop** - Calls updatePanelStatus() every 500ms
5. **Added click handler** - Clicking panel status dumps full status to console

## Usage Examples

### For Developers

Monitor panel initialization:
```bash
# Open browser console (F12)
# Watch for panel loading messages during startup
```

Check panel health during runtime:
```javascript
// In browser console
Module._bespoke_log_all_panels_status();
```

### For Users

Simply look at the panel status widget in the top-right corner:
- All panels should show as "Loaded" after startup
- The active panel should show as "Running" with increasing frame counts
- Switch between panels and verify the status updates correctly

## Troubleshooting

### Panel shows "Not Loaded"
- Check console for initialization errors
- Verify WebGPU is properly initialized
- Check for JavaScript errors during startup

### Panel shows "Loaded" but not "Running"
- This is normal for inactive panels
- Only the currently selected panel should show as "Running"
- Try clicking on the panel tab to activate it

### Frame count not increasing
- Panel rendering may have stopped
- Check for WebGPU errors in console
- Check browser performance/throttling

### Status display not appearing
- JavaScript may not have loaded properly
- Check console for errors
- Verify Module functions are available

## Technical Details

### Panel State Machine
```
[Not Loaded] --> markPanelLoaded() --> [Loaded]
[Loaded] --> markPanelRunning() --> [Running]
[Running] --> (stays running while active)
```

### Frame Count Tracking
- Incremented on every render of the active panel
- Resets to 0 when panel is marked as loaded
- Used to detect panel activity

### Periodic Logging
- Logs every 300 frames (~5 seconds at 60fps)
- Helps track long-running sessions
- Useful for performance monitoring

## Future Enhancements

Potential additions:
- Panel-specific performance metrics
- Panel error tracking and reporting
- Panel state persistence
- Panel health monitoring
- CPU usage per panel
- Memory usage per panel
