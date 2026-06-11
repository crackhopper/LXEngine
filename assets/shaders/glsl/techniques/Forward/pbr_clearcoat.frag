#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common/pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif

layout(location = 0) out vec4 outColor;

const uint INVALID_TEXTURE_INDEX = 0xffffffffu;
const uint SCENE_TEXTURE_COUNT = 256u;

struct lxSceneMaterialRecord {
    vec4 baseColor;
    vec4 pbrParams;
    vec4 emissive;
    vec4 clearcoatParams;
    uint baseColorTexture;
    uint normalTexture;
    uint metallicRoughnessTexture;
    uint aoTexture;
    uint emissiveTexture;
    uint flags;
    uint reserved0;
    uint reserved1;
};

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(std430, set = 0, binding = 7) readonly buffer SceneMaterials {
    lxSceneMaterialRecord materials[];
};

layout(set = 0, binding = 11) uniform sampler2D SceneTextures[256];

layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

bool hasSceneTexture(uint textureIndex) {
    return textureIndex != INVALID_TEXTURE_INDEX &&
           textureIndex < SCENE_TEXTURE_COUNT;
}

vec4 sampleSceneTexture(uint textureIndex, vec2 uv) {
    return texture(SceneTextures[nonuniformEXT(textureIndex)], uv);
}

void main() {
    lxSceneMaterialRecord material = materials[0];

    vec4 albedo = material.baseColor;
    if (hasSceneTexture(material.baseColorTexture)) {
        albedo *= sampleSceneTexture(material.baseColorTexture, vUV);
    }

    float metallic = material.pbrParams.x;
    float roughness = material.pbrParams.y;

#ifdef HAS_METALLIC_ROUGHNESS
    vec4 mr = hasSceneTexture(material.metallicRoughnessTexture)
                  ? sampleSceneTexture(material.metallicRoughnessTexture, vUV)
                  : vec4(1.0);
    metallic *= mr.b;
    roughness *= mr.g;
#endif
    roughness = clamp(roughness, 0.04, 1.0);

    float ao = material.pbrParams.w;
#ifdef HAS_AO_MAP
    if (hasSceneTexture(material.aoTexture)) {
        ao *= sampleSceneTexture(material.aoTexture, vUV).r;
    }
#endif

    vec3 N = normalize(vNormal);

#ifdef HAS_NORMAL_MAP
    if (hasSceneTexture(material.normalTexture)) {
        vec3 tangentNormal =
            sampleSceneTexture(material.normalTexture, vUV).rgb * 2.0 - 1.0;
        N = normalize(vTBN * tangentNormal);
    }
#endif

    vec3 V = normalize(camera.eyePos - vWorldPos);
    vec3 L = normalize(-light.direction.xyz);

    LxPbrDirectInput pbrInput;
    pbrInput.baseColor = albedo.rgb;
    pbrInput.normal = N;
    pbrInput.viewDir = V;
    pbrInput.lightDir = L;
    pbrInput.lightColor = light.color.rgb * light.color.a;
    pbrInput.metallic = metallic;
    pbrInput.roughness = roughness;
    pbrInput.ao = ao;
    pbrInput.emissive = material.emissive.rgb;
#ifdef HAS_EMISSIVE_MAP
    if (hasSceneTexture(material.emissiveTexture)) {
        pbrInput.emissive =
            sampleSceneTexture(material.emissiveTexture, vUV).rgb;
    }
#endif

    LxPbrClearcoatInput clearcoat;
    clearcoat.factor = material.clearcoatParams.x;
    clearcoat.roughness = material.clearcoatParams.y;

    vec3 color = lxPbrLayeredClearcoatDirectLight(pbrInput, clearcoat);
    color += lxPbrEmissive(pbrInput);

    outColor = vec4(color, albedo.a);
}
