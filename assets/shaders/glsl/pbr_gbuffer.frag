#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif

layout(location = 0) out vec4 outAlbedoAlpha;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outMaterial;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float ao;
    float padding;
} material;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;

#ifdef HAS_NORMAL_MAP
layout(set = 1, binding = 2) uniform sampler2D normalMap;
#endif

#ifdef HAS_METALLIC_ROUGHNESS
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
#endif

#ifdef HAS_AO_MAP
layout(set = 1, binding = 4) uniform sampler2D aoMap;
#endif

#ifdef HAS_EMISSIVE_MAP
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;
#endif

void main() {
    vec4 albedo = texture(albedoMap, vUV) * material.baseColorFactor;

    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
#ifdef HAS_METALLIC_ROUGHNESS
    vec4 mr = texture(metallicRoughnessMap, vUV);
    metallic *= mr.b;
    roughness *= mr.g;
#endif
    roughness = clamp(roughness, 0.04, 1.0);

    float ao = material.ao;
#ifdef HAS_AO_MAP
    ao *= texture(aoMap, vUV).r;
#endif

    vec3 N = normalize(vNormal);
#ifdef HAS_NORMAL_MAP
    vec3 tangentNormal = texture(normalMap, vUV).rgb * 2.0 - 1.0;
    N = normalize(vTBN * tangentNormal);
#endif

    vec3 emissive = vec3(0.0);
#ifdef HAS_EMISSIVE_MAP
    emissive = texture(emissiveMap, vUV).rgb;
#endif

    outAlbedoAlpha = vec4(albedo.rgb, albedo.a);
    outNormalRoughness = vec4(normalize(N) * 0.5 + 0.5, roughness);
    outMaterial = vec4(metallic, ao, 0.0, 0.0);
    outAlbedoAlpha.rgb += emissive;
}
