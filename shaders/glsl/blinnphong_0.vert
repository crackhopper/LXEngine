#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
    int  enableLighting;
    int  enableSkinning; // 开启蒙皮
    int  padding[2];
} object;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 3, binding = 0) uniform Bones {
    mat4 bones[128];
} skin;

// 输入属性
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent; // xyz: 切线, w: 手性
layout(location = 4) in ivec4 inBoneIDs;
layout(location = 5) in vec4 inBoneWeights;

// 输出到 Fragment
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vUV;
layout(location = 2) out mat3 vTBN; // 直接传递整个矩阵

void main() {
    // 1. 骨骼动画计算
    mat4 skinMatrix = mat4(1.0);
    if (object.enableSkinning == 1) {
        skinMatrix = 
            inBoneWeights.x * skin.bones[inBoneIDs.x] +
            inBoneWeights.y * skin.bones[inBoneIDs.y] +
            inBoneWeights.z * skin.bones[inBoneIDs.z] +
            inBoneWeights.w * skin.bones[inBoneIDs.w];
    }

    mat4 finalModel = object.model * skinMatrix;
    vec4 worldPos = finalModel * vec4(inPosition, 1.0);
    
    gl_Position = camera.proj * camera.view * worldPos;
    // NDC_coord = proj * view * model (skin) * pos
    vWorldPos = worldPos.xyz;
    vUV = inUV;

    // 2. TBN 矩阵构建 (世界空间)
    // 使用法线矩阵处理非统一缩放
    mat3 normalMatrix = mat3(transpose(inverse(finalModel)));
    
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    // 重建副切线 B
    vec3 B = normalize(cross(N, T) * inTangent.w);

    vTBN = mat3(T, B, N);
}
