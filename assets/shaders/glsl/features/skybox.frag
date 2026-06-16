#version 450

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform samplerCube SkyboxMap;

layout(set = 1, binding = 1) uniform SkyboxUBO {
    vec3 color;
    float intensity;
    float rotation;
} skybox;

void main() {
    vec3 sampleDir = vec3(cos(skybox.rotation), 0.0, sin(skybox.rotation));
    vec3 radiance = texture(SkyboxMap, sampleDir).rgb;
    outColor = vec4(radiance * skybox.color * max(skybox.intensity, 0.0), 1.0);
}
