#version 450

layout(location = 0) in vec3 vDirection;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube SkyboxMap;

const float PI = 3.14159265359;

void main() {
    vec3 normal = normalize(vDirection);
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;
    const float delta = 0.25;
    for (float phi = 0.0; phi < 2.0 * PI; phi += delta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += delta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),
                                      sin(theta) * sin(phi),
                                      cos(theta));
            vec3 sampleVec = tangentSample.x * right +
                             tangentSample.y * up +
                             tangentSample.z * normal;
            irradiance += texture(SkyboxMap, sampleVec).rgb *
                          cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }
    irradiance = PI * irradiance / max(sampleCount, 1.0);
    outColor = vec4(irradiance, 1.0);
}
