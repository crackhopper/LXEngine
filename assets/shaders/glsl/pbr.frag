#version 450

#include "common/pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif

layout(location = 0) out vec4 outColor;

// Camera
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

// Material parameters
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float ao;
    float padding;
} material;

// Albedo texture (always present)
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

void main() {
    // Base color
    vec4 albedo = texture(albedoMap, vUV) * material.baseColorFactor;

    // Metallic / roughness
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

    // Normal
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
    pbrInput.lightColor = light.color.rgb;
    pbrInput.metallic = metallic;
    pbrInput.roughness = roughness;
    pbrInput.ao = ao;
    pbrInput.emissive = vec3(0.0);
#ifdef HAS_EMISSIVE_MAP
    pbrInput.emissive = texture(emissiveMap, vUV).rgb;
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
