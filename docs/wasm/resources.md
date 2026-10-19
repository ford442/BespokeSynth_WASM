# WASM resource packaging

The browser build does **not** preload the full desktop `resource/` tree.

## What ships in the default demo

Emscripten packs `wasm/resource-pack/` into `BespokeSynthWASM.data` at `/resource`:

| Path | Purpose |
|------|---------|
| `userdata_original/scales.json` | Scale data |
| `userdata_original/savestate/wasm-starter.bsk` | Starter patch (`?patch=starter`, `bespoke_load_layout`) |
| `userdata_original/savestate/wasm-drumkit.bsk` | Drum-kit demo (`?patch=drumkit`): drumseq → four kit samplers → mixer → compressor |
| `kits/basic/{kick,snare,hat,tom}.wav` | Bundled one-shot drum kit, synthesized by `scripts/gen_drum_kit.py`; decoded on first use ([samples.md](samples.md#bundled-drum-kit)) |
| `userdata_original/layouts/blank.json` | Blank layout stub |
| `userdata_original/userdata_version.txt` | Version marker |
| `fonts/ui_font.bsdf` | SDF UI font atlas + glyph metrics (`SdfFont`), baked by `scripts/bake_sdf_font.sh` |

Webpack copies the same pack to `dist/resource/` for HTTP access.

## What stays desktop-only

Under repo-root `resource/` (not in the WASM pack):

- Desktop example `.bsk` savestates (~21 MB)
- Desktop drum kits (~10 MB) — not bundled. A small synthesized four-piece kit (`kits/basic/`, ~100 KB) is; for anything else load your own WAV/FLAC/MP3 by drag-and-drop (see [samples.md](samples.md#bundled-drum-kit))
- Controllers, python scripts, tooltips, TTF fonts

TTFs never ship. `scripts/bake_sdf_font.sh` compiles a host tool
(`wasm/tools/sdf_font_baker`, stb_truetype from `libs/nanovg`) that bakes
`resource/frabk.ttf` (Iosevka fallback for flat/sharp) into a single-channel
SDF atlas: ASCII plus the UI symbols, 32 px em, 4 px spread, 512x160 R8. If the
atlas is missing or corrupt, both renderers fall back to the pixel font compiled
into the `.wasm`.

## Regenerating the pack

```bash
./scripts/prepare_wasm_resource_pack.sh
```

## Size budget

| Artifact | Before (full `resource/`) | After (minimal pack) | Target |
|----------|---------------------------|----------------------|--------|
| `BespokeSynthWASM.data` | ~32 MB | **~4 KB** | < 5 MB |
| Full desktop `resource/` | ~32 MB | unchanged on disk | N/A |

### SDF font atlas

| Pack | `BespokeSynthWASM.data` | Font data |
|------|--------------------------|-----------|
| 5x7 pixel font only | 4,156 bytes | 2,020 bytes of glyph columns inside the `.wasm` |
| + `fonts/ui_font.bsdf` | 93,428 bytes | 89,272 bytes (81,920 atlas + metrics/kerning); ~35 KB gzip |

`wasm/CMakeLists.txt` lists the pack files in `LINK_DEPENDS`, so re-baking the
atlas relinks and refreshes the `.data` bundle.

Measure after every packaging change:

```bash
stat -c '%s' wasm/dist/BespokeSynthWASM.data wasm/dist/BespokeSynthWASM.wasm
```
