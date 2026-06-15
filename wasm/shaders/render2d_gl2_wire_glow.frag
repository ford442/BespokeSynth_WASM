#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float dist = abs(vTexcoord.y - 0.5) * 2.0;
    float core = smoothstep(0.3, 0.0, dist);
    float glow = smoothstep(1.0, 0.0, dist) * 0.5;
    vec4 color = vColor;
    color.a *= core + glow;
    fragColor = color;
}
