#version 450

layout(location = 0) in vec3 vDirection;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D EquirectangularMap;

const vec2 kInvAtan = vec2(0.15915494309189535, 0.3183098861837907);

vec2 sampleSphericalMap(vec3 direction) {
    vec2 uv = vec2(atan(direction.z, direction.x), asin(direction.y));
    uv *= kInvAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec3 direction = normalize(vDirection);
    outColor = vec4(texture(EquirectangularMap, sampleSphericalMap(direction)).rgb, 1.0);
}
