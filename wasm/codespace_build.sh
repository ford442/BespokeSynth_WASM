#!/bin/bash

# BespokeSynth WASM build helper for GitHub Codespaces.
# Installs the pinned Emscripten SDK when needed, then delegates to build.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
EMSCRIPTEN_VERSION="$(tr -d '[:space:]' < "$PROJECT_ROOT/.emscripten-version")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== BespokeSynth WASM Codespaces Build ===${NC}"

version_ge() {
    # True when $1 >= $2 (semantic version).
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

# Same acceptance rule as build.sh (CI additionally pins the exact version): Emscripten
# $EMSCRIPTEN_VERSION or newer, with the emdawnwebgpu port.
emcc_usable() {
    local found
    if ! command -v emcc &> /dev/null; then
        echo "emcc is not on PATH"
        return 1
    fi
    found="$(emcc --version | head -n 1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n 1 || true)"
    if [ -z "$found" ] || ! version_ge "$found" "$EMSCRIPTEN_VERSION"; then
        echo "emcc on PATH is ${found:-unknown}; need $EMSCRIPTEN_VERSION or newer"
        return 1
    fi
    if ! emcc --use-port=emdawnwebgpu:help >/dev/null 2>&1; then
        echo "emcc $found does not provide the emdawnwebgpu port"
        return 1
    fi
    echo "Using Emscripten $found"
}

if ! emcc_usable; then
    EMSDK_DIR="${EMSDK:-/workspaces/emsdk}"
    if [ ! -f "$EMSDK_DIR/emsdk" ]; then
        echo -e "${YELLOW}Installing Emscripten $EMSCRIPTEN_VERSION into $EMSDK_DIR...${NC}"
        git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    fi
    "$EMSDK_DIR/emsdk" install "$EMSCRIPTEN_VERSION"
    "$EMSDK_DIR/emsdk" activate "$EMSCRIPTEN_VERSION"
    export EMSDK="$EMSDK_DIR"
    # shellcheck disable=SC1091
    source "$EMSDK_DIR/emsdk_env.sh"
    if ! emcc_usable; then
        echo -e "${RED}Error: Emscripten $EMSCRIPTEN_VERSION from $EMSDK_DIR is not usable${NC}"
        exit 1
    fi
fi

exec "$SCRIPT_DIR/build.sh" "$@"
