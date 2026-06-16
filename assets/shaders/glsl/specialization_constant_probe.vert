#version 450

layout(constant_id = 17) const bool test_feature_a = true;

layout(location = 0) out vec2 vUV;

void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    vec2 position = positions[gl_VertexIndex];
    vUV = test_feature_a ? position * 0.5 + 0.5 : vec2(0.0);
    gl_Position = vec4(position, 0.0, 1.0);
}
