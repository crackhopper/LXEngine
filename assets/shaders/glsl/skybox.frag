#version 450

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 1, binding = 0) uniform samplerCube SkyboxMap;

layout(set = 2, binding = 0) uniform EnvironmentUBO {
    vec4 params;
} environment;

void main() {
    vec4 viewPos = inverse(camera.proj) * vec4(vNdc, 1.0, 1.0);
    vec3 viewDir = normalize(viewPos.xyz / max(abs(viewPos.w), 0.0001));
    vec3 worldDir = normalize(transpose(mat3(camera.view)) * viewDir);
    vec3 hdr = texture(SkyboxMap, worldDir).rgb * max(environment.params.x, 0.0);
    outColor = vec4(hdr, 1.0);
}
