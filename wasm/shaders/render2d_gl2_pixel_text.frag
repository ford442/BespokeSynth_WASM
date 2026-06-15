#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
const int FONT_GLYPHS = 94; /* placeholder, actual substituted at runtime with kPixelFontGlyphCount */
uniform int uFontCols[475];
void main() {
    int charIdx = clamp(int(floor(vTexcoord.x)), 0, FONT_GLYPHS - 1);
    float localX = fract(vTexcoord.x);
    int px = clamp(int(localX * 5.0), 0, 4);
    int py = clamp(int(vTexcoord.y * 7.0), 0, 6);
    int colData = uFontCols[charIdx * 5 + px];
    int pixelOn = (colData >> py) & 1;
    if (pixelOn == 0) {
        discard;
    }
    fragColor = vColor;
}
