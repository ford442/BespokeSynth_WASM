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

if ! command -v emcc &> /dev/null; then
    EMSDK_DIR="${EMSDK:-/workspaces/emsdk}"
    if [ ! -f "$EMSDK_DIR/emsdk" ]; then
        echo -e "${YELLOW}Installing Emscripten $EMSCRIPTEN_VERSION into $EMSDK_DIR...${NC}"
        git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    fi
    "$EMSDK_DIR/emsdk" install "$EMSCRIPTEN_VERSION"
    "$EMSDK_DIR/emsdk" activate "$EMSCRIPTEN_VERSION"
    export EMSDK="$EMSDK_DIR"
fi

exec "$SCRIPT_DIR/build.sh" "$@"
