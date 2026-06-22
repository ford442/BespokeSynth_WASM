#version 300 es
precision highp float;
in vec2 vTexcoord;
in vec4 vColor;
out vec4 fragColor;
void main() {
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexcoord, center);
    vec2 lightDir = normalize(vec2(-0.5, -0.5));
    vec2 normal = normalize(vTexcoord - center);
    float highlight = max(0.0, dot(normal, lightDir));
    vec4 color = vColor;
    color.rgb += highlight * 0.3;
    float edgeDark = smoothstep(0.3, 0.5, dist);
    color.rgb *= 1.0 - edgeDark * 0.3;
    fragColor = color;
}
