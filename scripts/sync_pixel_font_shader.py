#!/usr/bin/env python3
"""Sync pixel_font_glyphs.inc into WebGPURenderer.cpp and render2d.wgsl."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "wasm/include/BespokeWasm/pixel_font_glyphs.inc"
WGSL_CPP = ROOT / "wasm/src/WebGPURenderer.cpp"
WGSL_REF = ROOT / "wasm/shaders/render2d.wgsl"

BEGIN = "// @pixel-font-glyph-data-begin"
END = "// @pixel-font-glyph-data-end"


def parse_inc(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    return [int(x) for x in re.findall(r"(\d+)u", text)]


def format_wgsl_block(values: list[int], indent: str = "    ") -> str:
    glyph_count = len(values) // 5
    lines = [
        f"{indent}// 5x7 bitmap font: ASCII 32-126 + extended glyphs ({glyph_count} total)",
        f"{indent}const FONT_COLS = array<u32, {len(values)}>(",
    ]
    for i in range(0, len(values), 5):
        chunk = ", ".join(f"{v}u" for v in values[i : i + 5])
        lines.append(f"{indent}    {chunk},")
    lines.append(f"{indent});")
    lines.append("")
    lines.append(f"{indent}// Pixel text shader - renders 5x7 bitmap font glyphs")
    lines.append(f"{indent}// texcoord.x = float(charIndex) + localX")
    lines.append(f"{indent}// charIndex: 0-{glyph_count - 1}")
    lines.append(f"{indent}@fragment")
    lines.append(f"{indent}fn fs_pixel_text(input: VertexOutput) -> @location(0) vec4<f32> {{")
    lines.append(f"{indent}    let charIdx = clamp(i32(floor(input.texcoord.x)), 0, {glyph_count - 1});")
    lines.append(f"{indent}    let localX = fract(input.texcoord.x);")
    lines.append("")
    lines.append(f"{indent}    let px = clamp(i32(localX * 5.0), 0, 4);")
    lines.append(f"{indent}    let py = clamp(i32(input.texcoord.y * 7.0), 0, 6);")
    lines.append("")
    lines.append(f"{indent}    let colData = FONT_COLS[charIdx * 5 + px];")
    lines.append(f"{indent}    let pixelOn = (colData >> u32(py)) & 1u;")
    lines.append("")
    lines.append(f"{indent}    if (pixelOn == 0u) {{")
    lines.append(f"{indent}        return vec4<f32>(0.0, 0.0, 0.0, 0.0);")
    lines.append(f"{indent}    }}")
    lines.append("")
    lines.append(f"{indent}    return input.color;")
    lines.append(f"{indent}}}")
    return "\n".join(lines)


def replace_block(text: str, block: str) -> str:
    pattern = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.DOTALL)
    replacement = f"{BEGIN}\n{block}\n{END}"
    if not pattern.search(text):
        raise RuntimeError(f"Markers not found: {BEGIN} ... {END}")
    return pattern.sub(replacement, text)


def main() -> int:
    if not INC.exists():
        print(f"Missing {INC}", file=sys.stderr)
        return 1

    values = parse_inc(INC)
    if len(values) % 5 != 0:
        print(f"Glyph data length {len(values)} is not a multiple of 5", file=sys.stderr)
        return 1

    block = format_wgsl_block(values)

    for path in (WGSL_CPP, WGSL_REF):
        original = path.read_text(encoding="utf-8")
        updated = replace_block(original, block)
        if updated != original:
            path.write_text(updated, encoding="utf-8")
            print(f"Updated {path.relative_to(ROOT)}")
        else:
            print(f"Already up to date: {path.relative_to(ROOT)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
