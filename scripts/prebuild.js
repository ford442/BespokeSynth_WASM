#!/usr/bin/env node
/**
 * Cross-platform prebuild script
 * Attempts to build WASM module if Emscripten is available
 */

const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

const wasmDir = path.join(__dirname, '..', 'wasm');
const buildScript = path.join(wasmDir, 'build.sh');

// Check if build script exists
if (!fs.existsSync(buildScript)) {
  console.log('WASM build script not found. Skipping WASM build.');
  process.exit(0);
}

// Try to build WASM
try {
  console.log('Attempting to build WASM module...');
  execSync('cd wasm && ./build.sh', {
    stdio: 'inherit',
    shell: true,
  });
  console.log('WASM build completed successfully.');
} catch (error) {
  console.log('WASM build skipped (requires Emscripten).');
  console.log('You can still build the web app with: npm run build:web-only');
  // Don't fail the build - just skip WASM
  process.exit(0);
}
