#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common/pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif
layout(location = 6) flat in uint vMaterialIndex;

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

// Camera
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(std430, set = 0, binding = 7) readonly buffer SceneMaterials {
    lxSceneMaterialRecord materials[];
};

#ifdef HAS_SCENE_TEXTURES
layout(set = 0, binding = 11) uniform sampler2D SceneTextures[256];
#endif

// Light
layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

#ifdef HAS_IBL
layout(set = 3, binding = 0) uniform samplerCube IrradianceMap;
layout(set = 3, binding = 1) uniform samplerCube PrefilteredEnvMap;
layout(set = 3, binding = 2) uniform sampler2D BrdfLut;
layout(set = 3, binding = 3) uniform EnvironmentUBO {
    vec4 params; // x: IBL intensity, y: prefiltered mip count
} environment;
#endif

bool hasSceneTexture(uint textureIndex) {
#ifdef HAS_SCENE_TEXTURES
    return textureIndex != INVALID_TEXTURE_INDEX &&
           textureIndex < SCENE_TEXTURE_COUNT;
#else
    return false;
#endif
}

vec4 sampleSceneTexture(uint textureIndex, vec2 uv) {
#ifdef HAS_SCENE_TEXTURES
    return texture(SceneTextures[nonuniformEXT(textureIndex)], uv);
#else
    return vec4(1.0);
#endif
}

void main() {
    lxSceneMaterialRecord material = materials[vMaterialIndex];

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

    // Normal
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

    vec3 Lo = lxPbrDirectLight(pbrInput);
    vec3 F0 = lxPbrF0(albedo.rgb, metallic);

    vec3 ambient = vec3(0.0);
#ifdef HAS_IBL
    // Ambient/IBL is opt-in through the material variant and scene
    // EnvironmentUBO. Direct-light compare materials leave HAS_IBL disabled.
    float iblIntensity = max(environment.params.x, 0.0);
    if (iblIntensity > 0.0) {
        float NdotV = max(dot(N, V), 0.0);
        vec3 F_ibl = lxFresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD_ibl = (vec3(1.0) - F_ibl) * (1.0 - metallic);

        vec3 irradiance = texture(IrradianceMap, N).rgb;
        vec3 diffuse = irradiance * albedo.rgb;

        vec3 R = reflect(-V, N);
        float maxMip = max(environment.params.y - 1.0, 0.0);
        vec3 prefilteredColor =
            textureLod(PrefilteredEnvMap, R, roughness * maxMip).rgb;
        vec2 brdf = texture(BrdfLut, vec2(NdotV, roughness)).rg;
        vec3 specularIbl = prefilteredColor * (F_ibl * brdf.x + brdf.y);

        ambient = (kD_ibl * diffuse + specularIbl) * ao * iblIntensity;
    }
#endif

    vec3 color = ambient + Lo;
    color += lxPbrEmissive(pbrInput);

    outColor = vec4(color, albedo.a);
}
