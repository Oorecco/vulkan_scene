#version 450
// ui.frag — Either solid color rect or font glyph sampling.
// Push constant selects the mode (0=solid, 1=text).

layout(push_constant) uniform UIMode {
    int mode; // 0 = solid, 1 = text
} pc;

layout(binding = 0) uniform sampler2D fontAtlas;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    if (pc.mode == 1) {
        // R8 font atlas — only use the red channel as alpha
        float alpha = texture(fontAtlas, fragUV).r;
        if (alpha < 0.05) discard;
        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
    } else {
        outColor = fragColor;
    }
}
