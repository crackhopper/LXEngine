#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common/material_surface.glsl"
#include "common/material_bsdf.glsl"
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
layout(location = 6) flat in uint vMaterialIndex;

layout(location = 0) out vec4 outAlbedoAlpha;
layout(location = 1) out vec4 outNormalRoughness;
layout(location = 2) out vec4 outMaterial;

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
        lxLoadMaterialSurface(vMaterialIndex, vUV, geometricNormal,
                              tangentFrame);

    vec4 albedo =
        vec4(max(surface.baseColor, vec3(0.0)), clamp(surface.alpha, 0.0, 1.0));
    float metallic = clamp(surface.metallic, 0.0, 1.0);
    float roughness = surface.roughness;
    roughness = clamp(roughness, 0.04, 1.0);
    float ao = clamp(surface.ao, 0.0, 1.0);
    vec3 N = normalize(surface.normal);
    vec3 emissive = max(surface.emissive, vec3(0.0));

    LxBsdfEvaluateInput bsdfInput;
    bsdfInput.normal = N;
    bsdfInput.wi = N;
    bsdfInput.wo = N;
    bsdfInput.baseColor = albedo.rgb;
    bsdfInput.metallic = metallic;
    bsdfInput.roughness = roughness;
    bsdfInput.ao = ao;
    bsdfInput.emissive = emissive;
    LxBsdfEvaluateOutput bsdf = lxEvaluateBsdf(bsdfInput);
    vec3 bsdfBaseColor = max(bsdf.value * LX_BSDF_PI, vec3(0.0));

    outAlbedoAlpha = vec4(bsdfBaseColor, albedo.a);
    outNormalRoughness = vec4(normalize(N) * 0.5 + 0.5, roughness);
    outMaterial = vec4(metallic, ao, 0.0, 0.0);
    outAlbedoAlpha.rgb += emissive;
}
