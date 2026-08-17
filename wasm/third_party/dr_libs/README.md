# dr_libs

Single-header public-domain audio decoders from
[mackron/dr_libs](https://github.com/mackron/dr_libs).

Used by the WASM sample loader (`SampleDecode.cpp`) so WAV/FLAC/MP3 decode is
identical for the SDL2 backend, the AudioWorklet path, and offline render.

Do not clang-format these headers.
