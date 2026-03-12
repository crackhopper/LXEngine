#version 450

layout(push_constant) uniform ObjectPC
{
    mat4 model;
    int enableSkinning;
    int padding[3];
} object;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
    vec3 eyePos;
    float padding;
} camera;

layout(set = 1, binding = 0) uniform MaterialUBO
{
    int enableLighting;
    int enableTexture;
    int enableNormalMap;
    int padding0; // 填充

    vec3 baseColor;
    float shininess;
} material;

layout(set = 2, binding = 0) uniform Bones
{
    mat4 bones[128];
} skin;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;
layout(location = 4) in vec4 inTangent;

layout(location = 5) in ivec4 inBoneIDs;
layout(location = 6) in vec4 inBoneWeights;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vColor;
layout(location = 4) out mat3 TBN;

void main()
{
    mat4 skinMatrix = mat4(1.0);

    if (material.enableSkinning == 1)
    {
        skinMatrix =
            inBoneWeights.x * skin.bones[inBoneIDs.x]
                + inBoneWeights.y * skin.bones[inBoneIDs.y]
                + inBoneWeights.z * skin.bones[inBoneIDs.z]
                + inBoneWeights.w * skin.bones[inBoneIDs.w];
    }

    vec4 localPos = skinMatrix * vec4(inPosition, 1.0);
    vec4 worldPos = object.model * localPos;

    gl_Position = camera.proj * camera.view * worldPos;

    vWorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(transpose(inverse(object.model)));

    vec3 N = normalize(normalMatrix * inNormal);
    vNormal = N;

    vUV = inUV;
    vColor = inColor;

    if (material.enableNormalMap == 1)
    {
        vec3 T = normalize(normalMatrix * inTangent.xyz);
        vec3 B = cross(N, T) * inTangent.w;
        TBN = mat3(T, B, N);
    }
}
