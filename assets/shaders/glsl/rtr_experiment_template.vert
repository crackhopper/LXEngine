#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
} object;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 vWorldNormal;
layout(location = 1) out vec2 vUV;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

    mat3 normalMatrix = mat3(transpose(inverse(object.model)));
    vWorldNormal = normalize(normalMatrix * inNormal);
    vUV = inUV;
}
