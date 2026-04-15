#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
} object;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

#ifdef USE_SKINNING
layout(set = 3, binding = 0) uniform Bones {
    mat4 bones[128];
} skin;
#endif

// Vertex inputs shrink with the enabled forward-shader variants.
layout(location = 0) in vec3 inPosition;
#ifdef USE_LIGHTING
layout(location = 1) in vec3 inNormal;
#endif
#ifdef USE_UV
layout(location = 2) in vec2 inUV;
#endif
#ifdef USE_NORMAL_MAP
layout(location = 3) in vec4 inTangent;
#endif
#ifdef USE_SKINNING
layout(location = 4) in ivec4 inBoneIDs;
layout(location = 5) in vec4 inBoneWeights;
#endif
#ifdef USE_VERTEX_COLOR
layout(location = 6) in vec4 inColor;
#endif

// Fragment varyings shrink with the same variant contract.
#ifdef USE_LIGHTING
layout(location = 0) out vec3 vWorldPos;
#endif
#ifdef USE_UV
layout(location = 1) out vec2 vUV;
#endif
#ifdef USE_VERTEX_COLOR
layout(location = 2) out vec4 vColor;
#endif
#ifdef USE_LIGHTING
layout(location = 3) out vec3 vWorldNormal;
#endif
#ifdef USE_NORMAL_MAP
layout(location = 4) out mat3 vTBN;
#endif

void main() {
    mat4 skinMatrix = mat4(1.0);
#ifdef USE_SKINNING
    skinMatrix =
        inBoneWeights.x * skin.bones[inBoneIDs.x] +
        inBoneWeights.y * skin.bones[inBoneIDs.y] +
        inBoneWeights.z * skin.bones[inBoneIDs.z] +
        inBoneWeights.w * skin.bones[inBoneIDs.w];
#endif

    mat4 finalModel = object.model * skinMatrix;
    vec4 worldPos = finalModel * vec4(inPosition, 1.0);

    gl_Position = camera.proj * camera.view * worldPos;

#ifdef USE_LIGHTING
    vWorldPos = worldPos.xyz;
#endif
#ifdef USE_UV
    vUV = inUV;
#endif
#ifdef USE_VERTEX_COLOR
    vColor = inColor;
#endif

#ifdef USE_LIGHTING
    mat3 normalMatrix = mat3(transpose(inverse(finalModel)));
    vWorldNormal = normalize(normalMatrix * inNormal);
#endif

#ifdef USE_NORMAL_MAP
    vec3 N = vWorldNormal;
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    vec3 B = normalize(cross(N, T) * inTangent.w);
    vTBN = mat3(T, B, N);
#endif
}
