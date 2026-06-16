#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 5, binding = 0) uniform BloomUBO {
    float threshold;
    float intensity;
    float radius;
} bloom;

void main() {
    vec3 color = texture(SceneColor, vUV).rgb;
    float brightness = max(max(color.r, color.g), color.b);
    float bloomMask = brightness > bloom.threshold ? 1.0 : 0.0;
    vec3 bloomColor = color * bloomMask * max(bloom.intensity, 0.0);
    outColor = vec4(color + bloomColor * max(bloom.radius, 0.0), 1.0);
}
