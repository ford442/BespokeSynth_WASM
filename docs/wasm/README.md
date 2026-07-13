# WASM contributor guide

Start here for the browser port. The WASM build is a distinct implementation from the desktop app: changes under `Source/` are not automatically included in the browser artifact.

1. Read [architecture.md](architecture.md) to find the TypeScript, bridge, renderer, and audio boundaries.
2. Use [roadmap.md](roadmap.md) to select an in-scope milestone.
3. Follow [webgl-fallback.md](webgl-fallback.md) for renderer debugging and screenshots.
4. Read [audio.md](audio.md), [module-porting.md](module-porting.md), or [python.md](python.md) before changing those systems.

Build with `npm run build:wasm` (Release) or `npm run build:wasm:debug`; run `npm run dev` for the browser UI. Historical reports are summarized in [changelog.md](changelog.md), not used as current implementation guidance.

Current system details and commands live in [`wasm/README.md`](../../wasm/README.md).
