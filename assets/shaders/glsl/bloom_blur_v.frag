#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D BloomSource;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(BloomSource, 0));
    vec3 color = texture(BloomSource, vUV).rgb * 0.227027;
    color += texture(BloomSource, vUV + vec2(0.0, texel.y * 1.384615)).rgb * 0.316216;
    color += texture(BloomSource, vUV - vec2(0.0, texel.y * 1.384615)).rgb * 0.316216;
    color += texture(BloomSource, vUV + vec2(0.0, texel.y * 3.230769)).rgb * 0.070270;
    color += texture(BloomSource, vUV - vec2(0.0, texel.y * 3.230769)).rgb * 0.070270;
    outColor = vec4(color, 1.0);
}
