#version 450

layout(constant_id = 23) const bool test_feature_b = false;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = test_feature_b ? vec4(vUV, 0.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
