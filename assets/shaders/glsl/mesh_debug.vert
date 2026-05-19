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

void main() {
    gl_Position = camera.proj * camera.view * object.model * vec4(inPosition, 1.0);
}
