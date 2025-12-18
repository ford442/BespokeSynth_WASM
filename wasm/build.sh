#!/bin/bash

# BespokeSynth WASM Build Script
# This script builds the WASM version of BespokeSynth using Emscripten

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== BespokeSynth WASM Build ===${NC}"

source ../../emsdk/emsdk_env.sh

# Check for Emscripten
if ! command -v emcc &> /dev/null; then
    echo -e "${RED}Error: Emscripten (emcc) not found${NC}"
    echo "Please install Emscripten and source emsdk_env.sh"
    echo "See: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

echo -e "${GREEN}Using Emscripten:${NC} $(emcc --version | head -n 1)"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${YELLOW}Configuring with CMake...${NC}"
emcmake cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBESPOKE_WASM_WEBGPU=ON \
    -DBESPOKE_WASM_SDL2_AUDIO=ON

# Build
echo -e "${YELLOW}Building...${NC}"
cmake --build . --parallel 55

# Copy output files
echo -e "${YELLOW}Copying output files...${NC}"
OUTPUT_DIR="$SCRIPT_DIR/dist"
mkdir -p "$OUTPUT_DIR"

if [ -f "BespokeSynthWASM.html" ]; then
    cp BespokeSynthWASM.html "$OUTPUT_DIR/index.html"
fi
if [ -f "BespokeSynthWASM.js" ]; then
    cp BespokeSynthWASM.js "$OUTPUT_DIR/"
fi
if [ -f "BespokeSynthWASM.wasm" ]; then
    cp BespokeSynthWASM.wasm "$OUTPUT_DIR/"
fi
if [ -d "resource" ]; then
    cp -r resource "$OUTPUT_DIR/"
fi

echo -e "${GREEN}Build complete!${NC}"
echo "Output files are in: $OUTPUT_DIR"
echo ""
echo "To run locally, start a web server:"
echo "  cd $OUTPUT_DIR"
echo "  python -m http.server 8000"
echo "Then open: http://localhost:8000/"
