#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube BakeEnvironmentSource;

void main() {
    vec2 xy = vUV * 2.0 - 1.0;
    vec3 direction = normalize(vec3(xy, 1.0));
    outColor = texture(BakeEnvironmentSource, direction);
}
