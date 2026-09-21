#!/usr/bin/env bash
# Bake the UI SDF font atlas into the WASM resource pack.
#
# Output: wasm/resource-pack/fonts/ui_font.bsdf (preloaded at /resource/fonts/ui_font.bsdf).
# The baker is a host tool (any C++17 compiler); nothing here ends up in the .wasm.
# Re-run after changing fonts, the charset, or bake parameters in the tool.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
OUT="${1:-$ROOT/wasm/resource-pack/fonts/ui_font.bsdf}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Franklin Gothic Book matches the desktop UI font (Source/SynthGlobals.cpp);
# Iosevka supplies glyphs it lacks (flat/sharp).
"$CXX" -std=c++17 -O2 -w \
  -I "$ROOT/libs/nanovg/nanovg" \
  "$ROOT/wasm/tools/sdf_font_baker/bake_sdf_font.cpp" \
  -o "$WORK/bake_sdf_font"

mkdir -p "$(dirname "$OUT")"
"$WORK/bake_sdf_font" \
  --font "$ROOT/resource/frabk.ttf" \
  --fallback "$ROOT/resource/iosevka-type-light.ttf" \
  --em 32 --spread 4 --width 512 \
  --out "$OUT"
