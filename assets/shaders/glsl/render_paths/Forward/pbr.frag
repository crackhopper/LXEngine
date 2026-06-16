#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common/material_surface.glsl"
#include "common/material_bsdf.glsl"
#include "common/pbr.glsl"
#include "common/tone_mapping.glsl"
#if defined(LX_MATERIAL_CONTRACT_SOURCE)
#include LX_MATERIAL_CONTRACT_SOURCE
#else
#error LX_MATERIAL_CONTRACT_SOURCE must be defined by the material shader variant
#endif

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) in mat3 vTBN;
#endif
layout(location = 6) flat in uint vMaterialRefIndex;

layout(location = 0) out vec4 outColor;

// Camera
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

// Light
layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

layout(set = 3, binding = 3) uniform EnvironmentUBO {
    vec4 params; // x: IBL intensity, y: prefiltered mip count
    vec4 ambientColorIntensity; // rgb: constant environment color, a: intensity
} environment;

layout(set = 4, binding = 0) uniform ToneMappingUBO {
    vec4 params; // x: enabled, y: exposure, z: mode, w: gamma
} toneMapping;

#ifdef HAS_IBL
layout(set = 3, binding = 0) uniform samplerCube IrradianceMap;
layout(set = 3, binding = 1) uniform samplerCube PrefilteredEnvMap;
layout(set = 3, binding = 2) uniform sampler2D BrdfLut;
#endif

mat3 makeFallbackTangentFrame(vec3 normal) {
    vec3 helper = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0)
                                        : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = normalize(cross(normal, tangent));
    return mat3(tangent, bitangent, normal);
}

void main() {
    vec3 geometricNormal = normalize(vNormal);
    mat3 tangentFrame = makeFallbackTangentFrame(geometricNormal);
#ifdef HAS_NORMAL_MAP
    tangentFrame = vTBN;
#endif
    LxMaterialSurface surface =
        lxLoadMaterialSurface(vMaterialRefIndex, vUV, geometricNormal,
                              tangentFrame);

    vec4 albedo =
        vec4(max(surface.baseColor, vec3(0.0)), clamp(surface.alpha, 0.0, 1.0));
    float metallic = clamp(surface.metallic, 0.0, 1.0);
    float roughness = surface.roughness;
    roughness = clamp(roughness, 0.04, 1.0);
    float ao = clamp(surface.ao, 0.0, 1.0);
    vec3 N = normalize(surface.normal);

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
    pbrInput.emissive = max(surface.emissive, vec3(0.0));

    LxBsdfEvaluateInput bsdfInput;
    bsdfInput.normal = N;
    bsdfInput.wi = L;
    bsdfInput.wo = V;
    bsdfInput.baseColor = albedo.rgb;
    bsdfInput.metallic = metallic;
    bsdfInput.roughness = roughness;
    bsdfInput.ao = ao;
    bsdfInput.emissive = pbrInput.emissive;
    LxBsdfEvaluateOutput bsdf = lxEvaluateBsdf(bsdfInput);

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = bsdf.value * pbrInput.lightColor * NdotL * ao;
    vec3 F0 = lxPbrF0(albedo.rgb, metallic);

    float NdotV = max(dot(N, V), 0.0);
    vec3 ambient = lxEvaluateConstantEnvironmentLight(
        albedo.rgb, metallic, roughness, ao, NdotV, F0,
        environment.ambientColorIntensity);
#ifdef HAS_IBL
    // Ambient/IBL is opt-in through the material variant and scene
    // EnvironmentUBO. Direct-light compare materials leave HAS_IBL disabled.
    float iblIntensity = max(environment.params.x, 0.0);
    if (iblIntensity > 0.0) {
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

    LxToneMappingParams toneParams;
    toneParams.enabled = toneMapping.params.x;
    toneParams.exposure = toneMapping.params.y;
    toneParams.mode = toneMapping.params.z;
    toneParams.gamma = toneMapping.params.w;
    outColor = vec4(lxApplyToneMapping(color, toneParams), albedo.a);
}
