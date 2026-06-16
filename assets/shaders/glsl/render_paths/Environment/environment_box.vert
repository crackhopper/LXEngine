#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 vWorldPos;

struct lxSceneObjectRecord {
    mat4 objectToWorld;
    mat4 worldToObject;
    vec4 boundsMin;
    vec4 boundsMax;
    uint visible;
    uint flags;
    uint visibilityMask;
    uint debugId;
};

struct lxSceneDrawRecord {
    uint objectIndex;
    uint materialIndex;
    uint meshIndex;
    uint materialRefIndex;
};

layout(std430, set = 0, binding = 8) readonly buffer SceneObjects {
    lxSceneObjectRecord objects[];
};

layout(std430, set = 0, binding = 9) readonly buffer SceneDraws {
    lxSceneDrawRecord draws[];
};

void main() {
    lxSceneDrawRecord draw = draws[gl_InstanceIndex];
    mat4 model = objects[draw.objectIndex].objectToWorld;
    vec4 worldPos = model * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;
    gl_Position = camera.proj * camera.view * worldPos;
}
