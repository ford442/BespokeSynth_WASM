#version 300 es
precision highp float;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexcoord;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewSize;
out vec2 vTexcoord;
out vec4 vColor;
void main() {
    float clipX = (aPosition.x / uViewSize.x) * 2.0 - 1.0;
    float clipY = 1.0 - (aPosition.y / uViewSize.y) * 2.0;
    gl_Position = vec4(clipX, clipY, 0.0, 1.0);
    vTexcoord = aTexcoord;
    vColor = aColor;
}
