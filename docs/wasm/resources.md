# WASM resource packaging

The browser build does **not** preload the full desktop `resource/` tree.

## What ships in the default demo

Emscripten packs `wasm/resource-pack/` into `BespokeSynthWASM.data` at `/resource`:

| Path | Purpose |
|------|---------|
| `userdata_original/scales.json` | Scale data |
| `userdata_original/savestate/wasm-starter.bsk` | Starter patch (`?patch=starter`, `bespoke_load_layout`) |
| `userdata_original/layouts/blank.json` | Blank layout stub |
| `userdata_original/userdata_version.txt` | Version marker |

Webpack copies the same pack to `dist/resource/` for HTTP access.

## What stays desktop-only

Under repo-root `resource/` (not in the WASM pack):

- Desktop example `.bsk` savestates (~21 MB)
- Drum kits (~10 MB) — deferred until a sampler adapter exists
- Controllers, python scripts, tooltips, TTF fonts

The UI uses the embedded pixel font; TTFs are not required at first paint.

## Regenerating the pack

```bash
./scripts/prepare_wasm_resource_pack.sh
```

## Size budget

| Artifact | Before (full `resource/`) | After (minimal pack) | Target |
|----------|---------------------------|----------------------|--------|
| `BespokeSynthWASM.data` | ~32 MB | **~4 KB** | < 5 MB |
| Full desktop `resource/` | ~32 MB | unchanged on disk | N/A |

Measure after every packaging change:

```bash
stat -c '%s' wasm/dist/BespokeSynthWASM.data wasm/dist/BespokeSynthWASM.wasm
```
