# PFFFT (vendored)

Julien Pommier's "Pretty Fast FFT": single-precision real and complex FFTs for
sizes of the form 2^a·3^b·5^c (a >= 5), with SSE / NEON / Altivec code paths.

- Source: <https://bitbucket.org/jpommier/pffft> (`master`), fetched 2026-10-19.
- Files: `pffft.c`, `pffft.h`, unmodified.
  - `pffft.c` sha256 `052633eaec428327b4e2991fd1c0a8967740df8d07566a34b9c234d9119620d8`
  - `pffft.h` sha256 `d6ac7f26f7c3f87ed2ad7f0264c09d72285526d937b4dccc5fc1c97645a0d55d`
- License: the FFTPACKv5 BSD-style license reproduced at the top of `pffft.c`
  (redistribution in source and binary form with the notice kept). Compatible
  with the project's GPL v3.

## How the WASM build uses it

`wasm/CMakeLists.txt` compiles `pffft.c` with `-msse` when `BESPOKE_WASM_SIMD` is on.
Emscripten maps the SSE1 intrinsics PFFFT uses (`xmmintrin.h`) onto WebAssembly
SIMD128, so the SSE path runs as wasm SIMD. Without SIMD it falls back to PFFFT's
scalar code. Callers go through `BespokeWasm/dsp/Fft.h` (`dsp::RealFft`), never
this header directly.

The choice over `Source/FFT.cpp` (Mayer FHT) and the analyzer's radix-2 FFT was made by
benchmark: see `test_fft_benchmark` in `wasm/tests/test_fft.cpp` and the record in
`docs/wasm/module-porting.md`.

To update: replace both files from upstream, refresh the hashes above, and rerun
`npm run test:unit` (the FFT round-trip and DFT checks).
