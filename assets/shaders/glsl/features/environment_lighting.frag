#version 450

layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform samplerCube SkyboxMap;

layout(set = 3, binding = 3) uniform EnvironmentLightingUBO {
    vec3 color;
    float intensity;
    float rotation;
} environmentLighting;

void main() {
    vec3 sampleDir =
        vec3(cos(environmentLighting.rotation), 0.0,
             sin(environmentLighting.rotation));
    vec3 radiance = texture(SkyboxMap, sampleDir).rgb;
    outColor = vec4(radiance * environmentLighting.color *
                        max(environmentLighting.intensity, 0.0),
                    1.0);
}
