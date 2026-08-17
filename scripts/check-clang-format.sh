#!/usr/bin/env bash
# Fail if any tracked C/C++ file under wasm/ or Source/ differs from clang-format.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found" >&2
  exit 1
fi

mapfile -t files < <(
  git ls-files 'wasm/**/*.cpp' 'wasm/**/*.h' 'wasm/**/*.c' 'Source/**/*.cpp' 'Source/**/*.h' 2>/dev/null \
    | grep -v '^libs/' | grep -v '^wasm/third_party/' || true
)

if ((${#files[@]} == 0)); then
  echo "No C/C++ files to check."
  exit 0
fi

diffs=()
for f in "${files[@]}"; do
  if ! diff -q <(clang-format "$f") "$f" >/dev/null 2>&1; then
    diffs+=("$f")
  fi
done

if ((${#diffs[@]} > 0)); then
  echo "clang-format check failed for ${#diffs[@]} file(s):" >&2
  printf '  %s\n' "${diffs[@]}" >&2
  exit 1
fi

echo "clang-format check passed (${#files[@]} files)."
