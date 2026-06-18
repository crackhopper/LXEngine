#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common/material_surface.glsl"
#include "common/material_bsdf.glsl"
#include "common/pbr.glsl"
#include "common/ibl_lighting.glsl"
#include "common/gamma_adjust.glsl"
#include "features/tone_mapping.glsl"
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

layout(constant_id = 0) const bool render_skybox = true;
layout(constant_id = 1) const bool enable_tonemapping = true;
layout(constant_id = 2) const bool enable_gamma = false;

#define LxForwardRenderSkybox render_skybox
#define LxForwardEnableTonemapping enable_tonemapping
#define LxForwardEnableGamma enable_gamma

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
    vec4 finalColor = albedo;
    if (lxGetMaterialType() == LX_MATERIAL_TYPE_UNLIT) {
        finalColor = albedo;
    } else {
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
        vec3 ambient = vec3(0.0);
        if (lxSurfaceLightingStandardPbrIblReady()) {
            ambient = evaluateIblStandardPbr(albedo.rgb, metallic, roughness,
                                             ao, N, V, NdotV, F0);
        }

        vec3 color = ambient + Lo;
        color += lxPbrEmissive(pbrInput);

        if (LxForwardEnableTonemapping) {
            color = lxApplyToneMappingCurve(color, toneMapping.exposure,
                                            toneMapping.mode);
        }
        finalColor = vec4(color, albedo.a);
    }
    if (LxForwardEnableGamma) {
        finalColor = lxApplyGammaAdjust(finalColor);
    }
    outColor = finalColor;
}
