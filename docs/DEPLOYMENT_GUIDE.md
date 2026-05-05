# Quick Guide: Rebuilding and Deploying WASM Files

## Overview

The code changes have been implemented to fix the initialization timeout issue. To complete the fix, you need to:

1. Rebuild the WASM module with the updated C++ code
2. Deploy the new WASM files to your hosting server

## Step 1: Install Emscripten (if not already installed)

```bash
# Clone the Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate the latest version
./emsdk install latest
./emsdk activate latest

# Set up environment variables for the current terminal session
source ./emsdk_env.sh

# Return to your project directory
cd /path/to/BespokeSynth_WASM
```

## Step 2: Build the WASM Module

```bash
# Navigate to the wasm directory
cd wasm

# Run the build script
./build.sh
```

Or manually with CMake:

```bash
cd wasm
mkdir -p build
cd build

# Configure with Emscripten
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBESPOKE_WASM_WEBGPU=ON \
    -DBESPOKE_WASM_SDL2_AUDIO=ON

# Build
cmake --build . --parallel

# The output files will be in wasm/build/:
# - BespokeSynthWASM.js
# - BespokeSynthWASM.wasm
```

## Step 3: Copy Output Files

After a successful build, copy the files from `wasm/build/` or `wasm/dist/`:

```bash
# From the wasm directory
ls -lh build/BespokeSynthWASM.*
# You should see:
# BespokeSynthWASM.js
# BespokeSynthWASM.wasm
```

## Step 4: Deploy to Your Server

Upload the new files to replace the existing ones:

**Old files** (to be replaced):
- `https://test.1ink.us/modular/wasm/BespokeSynthWASM.js`
- `https://test.1ink.us/modular/wasm/BespokeSynthWASM.wasm`

**Deploy method options:**

### Option A: Using SCP (if you have SSH access)
```bash
scp wasm/build/BespokeSynthWASM.js user@test.1ink.us:/path/to/modular/wasm/
scp wasm/build/BespokeSynthWASM.wasm user@test.1ink.us:/path/to/modular/wasm/
```

### Option B: Using FTP/SFTP
Use your FTP client (FileZilla, WinSCP, etc.) to upload:
- `wasm/build/BespokeSynthWASM.js` → `/modular/wasm/BespokeSynthWASM.js`
- `wasm/build/BespokeSynthWASM.wasm` → `/modular/wasm/BespokeSynthWASM.wasm`

### Option C: Using rsync
```bash
rsync -avz wasm/build/BespokeSynthWASM.* user@test.1ink.us:/path/to/modular/wasm/
```

### Option D: Web-based File Manager
If your hosting provider has a web-based file manager, use it to upload the files.

## Step 5: Verify the Deployment

### Test in Browser

1. Open your web application at: `https://test.1ink.us/` (or wherever it's hosted)
2. Open browser DevTools (F12) → Console tab
3. Look for these log messages:

**Expected successful initialization:**
```
Initializing BespokeSynth WASM...
loadWasmModule: script loaded
loadWasmModule: factory type = function
WASM module loaded successfully
BespokeSynth WASM: Initializing (width×height, 44100Hz, 512 samples)
WasmBridge: starting async WebGPU initialization (selector=#canvas)
WebGPUContext: initializeAsync started with selector=#canvas
Setting __bespoke_on_init_complete to receive async init completion
WebGPUContext: onAdapterRequest called, status=0
WebGPUContext: Adapter found, requesting device
WebGPUContext: onDeviceRequest called, status=0
WebGPUContext: Device acquired, assigning to context
WebGPUContext: onDeviceReady
BespokeSynth WASM: Initialization complete
WasmBridge: notifying JS of init complete (0)
Resolving __bespoke_on_init_complete with status 0
BespokeSynth initialized successfully (async)
```

### Check File Versions

Verify the files are accessible:
```bash
curl -I https://test.1ink.us/modular/wasm/BespokeSynthWASM.js
curl -I https://test.1ink.us/modular/wasm/BespokeSynthWASM.wasm
```

Check the file sizes match your build output.

## Troubleshooting

### Build Fails

**Error: "emcc not found"**
- Make sure you ran `source ./emsdk_env.sh`
- Try opening a new terminal and sourcing again

**Error: "WebGPU headers not found"**
- Ensure you're using Emscripten 3.1.50 or later
- The WebGPU port should be automatically downloaded on first use

**CMake configuration errors**
- Check that all dependencies are available
- Review the CMakeLists.txt for any missing paths

### Deployment Issues

**Files not updating on server**
- Clear browser cache (Ctrl+Shift+Delete)
- Add cache-busting query parameter: `?v=2`
- Check file timestamps on server

**CORS errors**
- Ensure CORS headers are properly configured on your server
- The `.wasm` file needs `Content-Type: application/wasm`

**WebGPU still timing out**
- Verify you uploaded BOTH .js and .wasm files
- Check browser console for specific error messages
- Ensure WebGPU is supported and enabled in your browser

## Additional Notes

### Browser Compatibility

WebGPU is required and supported in:
- Chrome/Edge 113+ (enabled by default)
- Firefox Nightly (behind flag)
- Safari Technology Preview (partial support)

### File Sizes

Typical file sizes after build:
- `BespokeSynthWASM.js`: ~100KB - 500KB
- `BespokeSynthWASM.wasm`: ~2MB - 10MB (depends on what's included)

If your files are significantly different, something may have gone wrong in the build.

### Build Time

The WASM build can take 2-10 minutes depending on your system. Be patient!

## Next Steps

After successful deployment:

1. Test the application thoroughly
2. Monitor console logs for any unexpected errors
3. Test on different browsers (Chrome, Edge, etc.)
4. Verify audio playback works correctly
5. Test WebGPU rendering performance

## Questions or Issues?

If you encounter problems:

1. Check the console logs for specific error messages
2. Review `WASM_FIX_SUMMARY.md` for technical details
3. Check the troubleshooting section in `README_NPM.md`
4. Open an issue on GitHub with:
   - Browser and version
   - Console error messages
   - Steps to reproduce

Good luck! 🎵
