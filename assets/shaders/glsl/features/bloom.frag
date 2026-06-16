#version 450

layout(location = 0) out vec4 outColor;

layout(set = 5, binding = 0) uniform BloomUBO {
    float threshold;
    float intensity;
    float radius;
} bloom;

void main() {
    outColor = vec4(bloom.threshold, bloom.intensity, bloom.radius, 1.0);
}
