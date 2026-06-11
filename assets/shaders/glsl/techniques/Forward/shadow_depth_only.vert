#version 450

layout(set = 0, binding = 0) uniform LightUBO {
    vec4 dir;
    vec4 color;
    mat4 shadowViewProj;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 cascadeDepthRanges;
    vec4 shadowParams;
} sceneLight;

#ifdef USE_SKINNING
layout(set = 3, binding = 0) uniform Bones {
    mat4 bones[128];
} skin;
#endif

layout(location = 0) in vec3 inPosition;
#ifdef USE_SKINNING
layout(location = 4) in ivec4 inBoneIDs;
layout(location = 5) in vec4 inBoneWeights;
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

    mat4 model = mat4(1.0);
    vec4 worldPos = model * skinMatrix * vec4(inPosition, 1.0);
    gl_Position = sceneLight.shadowViewProj * worldPos;
}
