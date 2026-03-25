#version 450
// scene.vert — Transform vertices. UV passed through for diffuse texture sampling.

layout(set=0, binding = 0) uniform UBOFrame {
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
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragPosWorld;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragColor;
layout(location = 3) out vec4 fragShadowCoord;
layout(location = 4) out vec2 fragUV;

void main() {
    vec4 worldPos   = pc.model * vec4(inPos, 1.0);
    fragPosWorld    = worldPos.xyz;
    fragNormal      = normalize(mat3(pc.normalMat) * inNormal);
    fragColor       = inColor;
    fragShadowCoord = ubo.lightVP * worldPos;
    fragUV          = inUV;
    gl_Position     = ubo.proj * ubo.view * worldPos;
}
