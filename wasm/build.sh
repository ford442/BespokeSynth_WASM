#!/bin/bash

# BespokeSynth WASM Build Script
# This script builds the WASM version of BespokeSynth using Emscripten.
# The required SDK version is pinned in ../.emscripten-version (used by CI).

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
EMSCRIPTEN_VERSION_FILE="$PROJECT_ROOT/.emscripten-version"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== BespokeSynth WASM ${BUILD_FLAVOR} Build ===${NC}"

read_required_emscripten_version() {
    if [ -f "$EMSCRIPTEN_VERSION_FILE" ]; then
        tr -d '[:space:]' < "$EMSCRIPTEN_VERSION_FILE"
    else
        echo "4.0.0"
    fi
}

version_ge() {
    # True when $1 >= $2 (semantic version).
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

source_emsdk_env() {
    local candidate
    for candidate in \
        "${EMSDK:+$EMSDK/emsdk_env.sh}" \
        "$PROJECT_ROOT/emsdk/emsdk_env.sh" \
        "$HOME/emsdk/emsdk_env.sh" \
        "$SCRIPT_DIR/../../emsdk/emsdk_env.sh"
    do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then
            # shellcheck disable=SC1090
            source "$candidate"
            return 0
        fi
    done
    return 1
}

# Source emsdk environment if available (support multiple layouts).
# emsdk_env.sh clears EM_CACHE, so restore an explicit caller override afterwards.
REQUESTED_EM_CACHE="${EM_CACHE:-}"
if source_emsdk_env; then
    :
else
    echo -e "${YELLOW}Warning: emsdk_env.sh not found; assume Emscripten is already on PATH${NC}"
    echo "Set EMSDK, install emsdk under the repo, \$HOME/emsdk, or source emsdk_env.sh manually."
fi
if [ -n "$REQUESTED_EM_CACHE" ]; then
    export EM_CACHE="$REQUESTED_EM_CACHE"
fi

# Check for Emscripten
if ! command -v emcc &> /dev/null; then
    echo -e "${RED}Error: Emscripten (emcc) not found${NC}"
    echo "Install and activate the version in .emscripten-version, then source emsdk_env.sh:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install \$(cat .emscripten-version)"
    echo "  ./emsdk activate \$(cat .emscripten-version)"
    echo "  source ./emsdk_env.sh"
    echo "See: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

EMCC_VERSION="$(emcc --version | head -n 1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1)"
REQUIRED_EMSCRIPTEN_VERSION="$(read_required_emscripten_version)"
echo -e "${GREEN}Using Emscripten:${NC} $(emcc --version | head -n 1)"

if ! version_ge "$EMCC_VERSION" "$REQUIRED_EMSCRIPTEN_VERSION"; then
    echo -e "${RED}Error: Emscripten $EMCC_VERSION is too old for this repository${NC}"
    echo "Required: Emscripten $REQUIRED_EMSCRIPTEN_VERSION or newer (emdawnwebgpu / modern WebGPU API)."
    echo "Install and activate the pinned SDK:"
    echo "  ./emsdk install $REQUIRED_EMSCRIPTEN_VERSION"
    echo "  ./emsdk activate $REQUIRED_EMSCRIPTEN_VERSION"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

if ! emcc --use-port=emdawnwebgpu:help >/dev/null 2>&1; then
    echo -e "${RED}Error: this Emscripten build does not provide the emdawnwebgpu port${NC}"
    echo "Upgrade to Emscripten $REQUIRED_EMSCRIPTEN_VERSION or newer."
    exit 1
fi

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
