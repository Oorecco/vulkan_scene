#version 450
// ui.vert — 2D UI overlay. NDC coordinates passed directly.
// Positions are already in [-1,1] range, no transform needed.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    fragUV    = inUV;
    fragColor = inColor;
    gl_Position = vec4(inPos, 0.0, 1.0);
}
