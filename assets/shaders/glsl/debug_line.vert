#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
    float _pad0;
} uCamera;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = inColor;
    gl_Position = uCamera.proj * uCamera.view * vec4(inPos, 1.0);
}
