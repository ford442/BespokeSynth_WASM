#!/usr/bin/env bash
# Verify every EMSCRIPTEN_KEEPALIVE symbol in wasm/src is listed in wasm/exports.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$ROOT/wasm/exports.txt"
SRC_DIR="$ROOT/wasm/src"

if [[ ! -f "$MANIFEST" ]]; then
  echo "Missing manifest: $MANIFEST" >&2
  exit 1
fi

manifest_symbols() {
  grep -v '^\s*#' "$MANIFEST" | grep -v '^\s*$' | sort -u
}

keepalive_symbols() {
  rg -o 'EMSCRIPTEN_KEEPALIVE[[:space:]]+(?:const[[:space:]]+)?(?:unsigned[[:space:]]+)?(?:char\*|int|float|void)[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*)' \
    "$SRC_DIR" \
    | sed -E 's/.*[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*)/\1/' \
    | sort -u
}

mapfile -t manifest < <(manifest_symbols)
mapfile -t keepalive < <(keepalive_symbols)

missing=()
for sym in "${keepalive[@]}"; do
  if ! printf '%s\n' "${manifest[@]}" | grep -qx "$sym"; then
    missing+=("$sym")
  fi
done

extra=()
for sym in "${manifest[@]}"; do
  case "$sym" in
    main|malloc|free) continue ;;
  esac
  if ! printf '%s\n' "${keepalive[@]}" | grep -qx "$sym"; then
    extra+=("$sym")
  fi
done

status=0
if ((${#missing[@]} > 0)); then
  echo "EMSCRIPTEN_KEEPALIVE symbols missing from wasm/exports.txt:" >&2
  printf '  %s\n' "${missing[@]}" >&2
  status=1
fi

if ((${#extra[@]} > 0)); then
  echo "wasm/exports.txt entries without EMSCRIPTEN_KEEPALIVE in wasm/src:" >&2
  printf '  %s\n' "${extra[@]}" >&2
  status=1
fi

if [[ $status -eq 0 ]]; then
  echo "WASM export manifest matches ${#keepalive[@]} KEEPALIVE symbols (+ main/malloc/free)."
fi

exit $status
