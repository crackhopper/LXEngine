#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif

layout(location = 0) out vec4 outAlbedoAlpha;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outMaterial;

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

layout(std430, set = 0, binding = 7) readonly buffer SceneMaterials {
    lxSceneMaterialRecord materials[];
};

layout(set = 0, binding = 11) uniform sampler2D SceneTextures[256];

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

    vec3 emissive = material.emissive.rgb;
#ifdef HAS_EMISSIVE_MAP
    if (hasSceneTexture(material.emissiveTexture)) {
        emissive = sampleSceneTexture(material.emissiveTexture, vUV).rgb;
    }
#endif

    outAlbedoAlpha = vec4(albedo.rgb, albedo.a);
    outNormalRoughness = vec4(normalize(N) * 0.5 + 0.5, roughness);
    outMaterial = vec4(metallic, ao, 0.0, 0.0);
    outAlbedoAlpha.rgb += emissive;
}
