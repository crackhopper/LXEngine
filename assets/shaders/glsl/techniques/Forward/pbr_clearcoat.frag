#version 450

#include "common/pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float ao;
    float clearcoatFactor;
    float clearcoatRoughness;
    float padding0;
    float padding1;
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

layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

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
    pbrInput.emissive = vec3(0.0);
#ifdef HAS_EMISSIVE_MAP
    pbrInput.emissive = texture(emissiveMap, vUV).rgb;
#endif

    LxPbrClearcoatInput clearcoat;
    clearcoat.factor = material.clearcoatFactor;
    clearcoat.roughness = material.clearcoatRoughness;

    vec3 color = lxPbrLayeredClearcoatDirectLight(pbrInput, clearcoat);
    color += lxPbrEmissive(pbrInput);

    outColor = vec4(color, albedo.a);
}
