#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
    uint materialIndex;
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

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) out mat3 vTBN;
#endif
layout(location = 6) flat out uint vMaterialIndex;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

    vWorldPos = worldPos.xyz;
    vUV = inUV;
    vMaterialIndex = object.materialIndex;

    mat3 normalMatrix = mat3(transpose(inverse(object.model)));
    vNormal = normalize(normalMatrix * inNormal);

#ifdef HAS_NORMAL_MAP
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    vec3 N = vNormal;
    vec3 B = normalize(cross(N, T) * inTangent.w);
    vTBN = mat3(T, B, N);
#endif
}
