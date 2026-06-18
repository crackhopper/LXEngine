#version 450

#include "common/pbr.glsl"
#include "common/ibl_lighting.glsl"
#include "common/tone_mapping.glsl"

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

layout(set = 4, binding = 0) uniform ToneMappingUBO {
    vec4 params; // x: enabled, y: exposure, z: mode, w: gamma
} toneMapping;

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

    vec3 F0 = lxPbrF0(albedoAlpha.rgb, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 color = lxPbrLayeredClearcoatDirectLight(pbrInput, clearcoat);
    if (lxSurfaceLightingStandardPbrIblReady()) {
        color += evaluateIblStandardPbr(albedoAlpha.rgb, metallic, roughness,
                                        ao, N, V, NdotV, F0);
    }

    LxToneMappingParams toneParams;
    toneParams.enabled = toneMapping.params.x;
    toneParams.exposure = toneMapping.params.y;
    toneParams.mode = toneMapping.params.z;
    toneParams.gamma = toneMapping.params.w;
    outColor = vec4(lxApplyToneMapping(color, toneParams), albedoAlpha.a);
}
