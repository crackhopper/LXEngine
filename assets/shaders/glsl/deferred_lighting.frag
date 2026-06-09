#version 450

#include "common/pbr.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D GBufferAlbedoAlpha;
layout(set = 0, binding = 1) uniform sampler2D GBufferNormalRoughness;
layout(set = 0, binding = 2) uniform sampler2D GBufferMaterial;
layout(set = 0, binding = 3) uniform sampler2D GBufferDepth;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

#ifdef HAS_IBL
layout(set = 3, binding = 0) uniform samplerCube IrradianceMap;
layout(set = 3, binding = 1) uniform samplerCube PrefilteredEnvMap;
layout(set = 3, binding = 2) uniform sampler2D BrdfLut;
layout(set = 3, binding = 3) uniform EnvironmentUBO {
    vec4 params;
} environment;
#endif

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    mat4 invViewProj = inverse(camera.proj * camera.view);
    vec4 world = invViewProj * clip;
    return world.xyz / max(world.w, 1.0e-6);
}

void main() {
    float depth = texture(GBufferDepth, vUV).r;
    if (depth >= 1.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 albedoAlpha = texture(GBufferAlbedoAlpha, vUV);
    vec4 normalRoughness = texture(GBufferNormalRoughness, vUV);
    vec4 material = texture(GBufferMaterial, vUV);

    vec3 N = normalize(normalRoughness.xyz * 2.0 - 1.0);
    float roughness = clamp(normalRoughness.w, 0.04, 1.0);
    float metallic = clamp(material.x, 0.0, 1.0);
    float ao = clamp(material.y, 0.0, 1.0);
    float clearcoatFactor = clamp(material.z, 0.0, 1.0);
    float clearcoatRoughness = clamp(material.w, 0.04, 1.0);

    vec3 worldPos = reconstructWorldPosition(vUV, depth);
    vec3 V = normalize(camera.eyePos - worldPos);
    vec3 L = normalize(-light.direction.xyz);

    LxPbrDirectInput pbrInput;
    pbrInput.baseColor = albedoAlpha.rgb;
    pbrInput.normal = N;
    pbrInput.viewDir = V;
    pbrInput.lightDir = L;
    pbrInput.lightColor = light.color.rgb * light.color.a;
    pbrInput.metallic = metallic;
    pbrInput.roughness = roughness;
    pbrInput.ao = ao;
    pbrInput.emissive = vec3(0.0);

    LxPbrClearcoatInput clearcoat;
    clearcoat.factor = clearcoatFactor;
    clearcoat.roughness = clearcoatRoughness;

    vec3 color = lxPbrLayeredClearcoatDirectLight(pbrInput, clearcoat);

#ifdef HAS_IBL
    float iblIntensity = max(environment.params.x, 0.0);
    if (iblIntensity > 0.0) {
        vec3 F0 = lxPbrF0(albedoAlpha.rgb, metallic);
        float NdotV = max(dot(N, V), 0.0);
        vec3 F_ibl = lxFresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD_ibl = (vec3(1.0) - F_ibl) * (1.0 - metallic);

        vec3 irradiance = texture(IrradianceMap, N).rgb;
        vec3 diffuse = irradiance * albedoAlpha.rgb;

        vec3 R = reflect(-V, N);
        float maxMip = max(environment.params.y - 1.0, 0.0);
        vec3 prefilteredColor =
            textureLod(PrefilteredEnvMap, R, roughness * maxMip).rgb;
        vec2 brdf = texture(BrdfLut, vec2(NdotV, roughness)).rg;
        vec3 specularIbl = prefilteredColor * (F_ibl * brdf.x + brdf.y);

        color += (kD_ibl * diffuse + specularIbl) * ao * iblIntensity;
    }
#endif

    outColor = vec4(color, albedoAlpha.a);
}
