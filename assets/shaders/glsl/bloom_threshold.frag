#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(set = 0, binding = 1) uniform BloomThresholdUBO {
    float threshold;
    float softKnee;
    float enabled;
    float padding;
} bloom;

void main() {
    vec3 color = texture(SceneColor, vUV).rgb;
    float brightness = max(max(color.r, color.g), color.b);
    float knee = max(bloom.softKnee, 0.0001);
    float soft = clamp((brightness - bloom.threshold + knee) / (2.0 * knee),
                       0.0, 1.0);
    float contribution = max(brightness - bloom.threshold, 0.0) +
                         soft * soft * knee;
    float scale = brightness > 0.0001 ? contribution / brightness : 0.0;
    outColor = vec4(color * scale * clamp(bloom.enabled, 0.0, 1.0), 1.0);
}
