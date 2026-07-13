#!/bin/bash

# BespokeSynth WASM Build Script
# This script builds the WASM version of BespokeSynth using Emscripten
# CI uses Emscripten 3.1.50; keep local builds on a compatible 3.1+ release.

set -euo pipefail

BUILD_FLAVOR="${1:-Release}"
case "$BUILD_FLAVOR" in
    Release|Debug|Profile) ;;
    *)
        echo "Usage: $0 [Release|Debug|Profile]"
        exit 2
        ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build/${BUILD_FLAVOR,,}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== BespokeSynth WASM ${BUILD_FLAVOR} Build ===${NC}"

# Source emsdk environment if available (support multiple layouts)
# emsdk_env.sh clears EM_CACHE, so restore an explicit caller override afterwards.
REQUESTED_EM_CACHE="${EM_CACHE:-}"
if [ -f "$PROJECT_ROOT/emsdk/emsdk_env.sh" ]; then
    source "$PROJECT_ROOT/emsdk/emsdk_env.sh"
elif [ -f "$SCRIPT_DIR/../../emsdk/emsdk_env.sh" ]; then
    source "$SCRIPT_DIR/../../emsdk/emsdk_env.sh"
else
    echo -e "${YELLOW}Warning: emsdk_env.sh not found; assume Emscripten is already on PATH${NC}"
fi
if [ -n "$REQUESTED_EM_CACHE" ]; then
    export EM_CACHE="$REQUESTED_EM_CACHE"
fi

# Check for Emscripten
if ! command -v emcc &> /dev/null; then
    echo -e "${RED}Error: Emscripten (emcc) not found${NC}"
    echo "Please install Emscripten and source emsdk_env.sh"
    echo "See: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

echo -e "${GREEN}Using Emscripten:${NC} $(emcc --version | head -n 1)"

echo -e "${YELLOW}Syncing pixel font shader data...${NC}"
python3 "$SCRIPT_DIR/../scripts/sync_pixel_font_shader.py"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake. Keep jobs bounded by default: the prior fixed 55-job
# build exhausted memory on small CI runners. BESPOKE_WASM_JOBS can override it.
CPU_COUNT="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
DEFAULT_JOBS=$(( CPU_COUNT < 8 ? CPU_COUNT : 8 ))
BUILD_JOBS="${BESPOKE_WASM_JOBS:-$DEFAULT_JOBS}"

# Profile has optimized code plus symbols and BESPOKE_WASM_FRAME_INSTRUMENTATION.
# Debug uses -O0/-g and ASSERTIONS=2; Release is assertions-free and Asyncify-free.
case "$BUILD_FLAVOR" in
    Debug) CMAKE_BUILD_TYPE=Debug ;;
    *) CMAKE_BUILD_TYPE=Release ;;
esac

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
emcmake cmake "$SCRIPT_DIR" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DBESPOKE_WASM_BUILD_FLAVOR="$BUILD_FLAVOR" \
    -DBESPOKE_WASM_WEBGPU=ON \
    -DBESPOKE_WASM_SDL2_AUDIO=ON

# Build
echo -e "${YELLOW}Building...${NC}"
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

# Copy output files
echo -e "${YELLOW}Copying output files...${NC}"
OUTPUT_DIR="$SCRIPT_DIR/dist"
mkdir -p "$OUTPUT_DIR"

if [ -f "$BUILD_DIR/BespokeSynthWASM.html" ]; then
    cp "$BUILD_DIR/BespokeSynthWASM.html" "$OUTPUT_DIR/index.html"
fi
if [ -f "$BUILD_DIR/BespokeSynthWASM.js" ]; then
    cp "$BUILD_DIR/BespokeSynthWASM.js" "$OUTPUT_DIR/"
fi
if [ -f "$BUILD_DIR/BespokeSynthWASM.wasm" ]; then
    cp "$BUILD_DIR/BespokeSynthWASM.wasm" "$OUTPUT_DIR/"
fi
if [ -f "$BUILD_DIR/BespokeSynthWASM.data" ]; then
    cp "$BUILD_DIR/BespokeSynthWASM.data" "$OUTPUT_DIR/"
fi
if [ -d "$BUILD_DIR/resource" ]; then
    cp -r "$BUILD_DIR/resource" "$OUTPUT_DIR/"
fi

echo -e "${GREEN}Build complete!${NC}"
echo "Output files are in: $OUTPUT_DIR"
echo ""
echo "To run locally, start a web server:"
echo "  cd $OUTPUT_DIR"
echo "  python -m http.server 8000"
echo "Then open: http://localhost:8000/"
