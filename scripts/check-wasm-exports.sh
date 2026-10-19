#!/usr/bin/env bash
# Verify every EMSCRIPTEN_KEEPALIVE symbol in wasm/src is listed in its module's manifest:
# wasm/exports_audio.txt for the AudioWorklet engine (AudioWorkletEngine.cpp), wasm/exports.txt
# for everything else.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$ROOT/wasm/src"
AUDIO_SOURCES=("$SRC_DIR/AudioWorkletEngine.cpp")

KEEPALIVE_RE='EMSCRIPTEN_KEEPALIVE[[:space:]]+(?:const[[:space:]]+)?(?:unsigned[[:space:]]+)?(?:char\*|int|float|double|void)[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*)'

manifest_symbols() {
  grep -v '^\s*#' "$1" | grep -v '^\s*$' | sort -u
}

# keepalive_symbols <file|dir>... ; ripgrep when present, else GNU grep -P.
keepalive_symbols() {
  if command -v rg >/dev/null 2>&1; then
    rg --no-filename -o "$KEEPALIVE_RE" "$@" || true
  else
    grep -rhoP "$KEEPALIVE_RE" "$@" || true
  fi | sed -E 's/.*[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*)/\1/' | sort -u
}

status=0

# check_manifest <manifest> <label> <source>...
check_manifest() {
  local manifest_file="$1" label="$2"
  shift 2
  if [[ ! -f "$manifest_file" ]]; then
    echo "Missing manifest: $manifest_file" >&2
    status=1
    return
  fi
  local manifest keepalive sym
  mapfile -t manifest < <(manifest_symbols "$manifest_file")
  mapfile -t keepalive < <(keepalive_symbols "$@")

  local missing=() extra=()
  for sym in "${keepalive[@]}"; do
    # Here-strings, not pipes: with pipefail, grep -q closing early SIGPIPEs printf.
    if ! grep -qx "$sym" <<< "$(printf '%s\n' "${manifest[@]}")"; then
      missing+=("$sym")
    fi
  done
  for sym in "${manifest[@]}"; do
    case "$sym" in
      main|malloc|free) continue ;;
    esac
    if ! grep -qx "$sym" <<< "$(printf '%s\n' "${keepalive[@]}")"; then
      extra+=("$sym")
    fi
  done

  if ((${#missing[@]} > 0)); then
    echo "EMSCRIPTEN_KEEPALIVE symbols missing from $label:" >&2
    printf '  %s\n' "${missing[@]}" >&2
    status=1
  fi
  if ((${#extra[@]} > 0)); then
    echo "$label entries without EMSCRIPTEN_KEEPALIVE in its sources:" >&2
    printf '  %s\n' "${extra[@]}" >&2
    status=1
  fi
  if ((${#missing[@]} == 0 && ${#extra[@]} == 0)); then
    echo "$label matches ${#keepalive[@]} KEEPALIVE symbols (+ main/malloc/free)."
  fi
}

main_sources=()
while IFS= read -r -d '' file; do
  skip=0
  for audio in "${AUDIO_SOURCES[@]}"; do
    [[ "$file" == "$audio" ]] && skip=1
  done
  ((skip)) || main_sources+=("$file")
done < <(find "$SRC_DIR" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -print0)

check_manifest "$ROOT/wasm/exports.txt" "wasm/exports.txt" "${main_sources[@]}"
check_manifest "$ROOT/wasm/exports_audio.txt" "wasm/exports_audio.txt" "${AUDIO_SOURCES[@]}"

exit $status
