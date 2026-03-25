#version 450
// shadow.vert — Render scene geometry from the light's point of view.
// Only needs position; normals and colors are irrelevant for depth.

layout(binding = 0) uniform UBOFrame {
    mat4 view;
    mat4 proj;
    mat4 lightVP;
    vec4 lightDir;
    vec4 camPos;
    vec4 skyCol;
    vec4 gndCol;
    vec4 fogParams;
    vec4 fogColor;
} ubo;

layout(push_constant) uniform PushConst {
    mat4 model;
    mat4 normalMat;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;  // unused but must match VBO layout
layout(location = 2) in vec3 inColor;   // unused but must match VBO layout

void main() {
    gl_Position = ubo.lightVP * pc.model * vec4(inPos, 1.0);
}
