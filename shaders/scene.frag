#version 450
// scene.frag — Lighting: hemisphere GI + directional + PCF shadows + fog + gamma.
// Set 0: UBO + shadow map (existing).
// Set 1: diffuse texture (new). White 1x1 fallback = vertex color only.

layout(set=0, binding = 0) uniform UBOFrame {
    mat4 view;
    mat4 proj;
    mat4 lightVP;
    vec4 lightDir;
    vec4 camPos;
    vec4 skyCol;     // rgb=color, a=intensity
    vec4 gndCol;     // rgb=color, a=intensity
    vec4 fogParams;  // x=start, y=end, z=density
    vec4 fogColor;
} ubo;

layout(set=0, binding = 1) uniform sampler2DShadow shadowMap;
layout(set=1, binding = 0) uniform sampler2D diffuseTex;

layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragColor;
layout(location = 3) in vec4 fragShadowCoord;
layout(location = 4) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// 5x5 PCF kernel — 25 samples for soft shadows
float shadowPCF(vec4 shadowCoord, vec3 N, vec3 L) {
    if (shadowCoord.w <= 0.0) return 1.0;
    vec3 projCoord = shadowCoord.xyz / shadowCoord.w;
    projCoord.xy   = projCoord.xy * 0.5 + 0.5;

    // Out of shadow map bounds = fully lit
    if (projCoord.x < 0.0 || projCoord.x > 1.0 ||
        projCoord.y < 0.0 || projCoord.y > 1.0 ||
        projCoord.z < 0.0 || projCoord.z > 1.0)
        return 1.0;

    float shadow    = 0.0;
    float NdL       = max(dot(N, L), 0.0);
    float bias      = max(0.00035 * (1.0 - NdL), 0.00008);
    vec2  texelSize = 1.0 / textureSize(shadowMap, 0);

    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            vec2  offset  = vec2(dx, dy) * texelSize;
            float sampleZ = projCoord.z - bias;
            shadow += texture(shadowMap,
                vec3(projCoord.xy + offset, sampleZ));
        }
    }
    return shadow / 25.0;
}

void main() {
    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) N = -N;

    // Diffuse texture * vertex color. White 1x1 = vertex color only (untextured path).
    vec4 diffSample = texture(diffuseTex, fragUV);
    vec3 baseColor  = fragColor * diffSample.rgb;
    // Alpha test for foliage/grass (alpha < 0.1 = discard)
    if (diffSample.a < 0.1) discard;

    // Hemisphere ambient GI
    float hemi = dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    vec3  amb  = mix(ubo.gndCol.rgb * ubo.gndCol.a,
                     ubo.skyCol.rgb * ubo.skyCol.a, hemi)
                 * baseColor;

    vec3  L    = normalize(ubo.lightDir.xyz);
    float NdL  = max(dot(N, L), 0.0);
    vec3  V    = normalize(ubo.camPos.xyz - fragPosWorld);
    vec3  H    = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.15;
    float shadow = shadowPCF(fragShadowCoord, N, L);

    vec3 col = amb + shadow * (baseColor * NdL + vec3(spec));

    // Gamma correction (sRGB output)
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));

    // Linear distance fog
    float dist   = length(ubo.camPos.xyz - fragPosWorld);
    float fogAmt = ubo.fogParams.z > 0.0
        ? clamp((dist - ubo.fogParams.x) / (ubo.fogParams.y - ubo.fogParams.x), 0.0, 1.0)
          * ubo.fogParams.z
        : 0.0;
    col = mix(col, ubo.fogColor.rgb, fogAmt);

    outColor = vec4(col, 1.0);
}
