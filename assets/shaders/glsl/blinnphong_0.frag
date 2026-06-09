#version 450

#ifdef USE_LIGHTING
layout(location = 0) in vec3 vWorldPos;
#endif
#ifdef USE_UV
layout(location = 1) in vec2 vUV;
#endif
#ifdef USE_VERTEX_COLOR
layout(location = 2) in vec4 vColor;
#endif
#ifdef USE_LIGHTING
layout(location = 3) in vec3 vWorldNormal;
#endif
#ifdef USE_NORMAL_MAP
layout(location = 4) in mat3 vTBN;
#endif

layout(push_constant) uniform ObjectPC {
    mat4 model;
} object;

#ifdef USE_LIGHTING
layout(set = 0, binding = 0) uniform LightUBO {
    vec4 dir;
    vec4 color;
    mat4 shadowViewProj;
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
    vec4 cascadeDepthRanges;
    vec4 shadowParams;
} sceneLight;
#ifdef HAS_SHADOWS
layout(set = 0, binding = 1) uniform sampler2D ShadowMap0;
layout(set = 0, binding = 2) uniform sampler2D ShadowMap1;
layout(set = 0, binding = 3) uniform sampler2D ShadowMap2;
layout(set = 0, binding = 4) uniform sampler2D ShadowMap3;
#endif
#endif

layout(set = 2, binding = 0) uniform MaterialUBO {
    vec3 baseColor;
    float shininess;

    float specularIntensity;
    float ambientIntensity;
    int enableAlbedo;
    int enableNormal;
    int debugShadowMode;
} material;

#ifdef USE_UV
layout(set = 2, binding = 1) uniform sampler2D albedoMap;
#endif
#ifdef USE_NORMAL_MAP
layout(set = 2, binding = 2) uniform sampler2D normalMap;
#endif

#ifdef USE_LIGHTING
layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;
#endif

layout(location = 0) out vec4 outColor;

vec3 computeBaseColor() {
    vec3 baseCol = material.baseColor;
#ifdef USE_VERTEX_COLOR
    baseCol *= vColor.rgb;
#endif
#ifdef USE_UV
    if (material.enableAlbedo == 1) {
        baseCol *= texture(albedoMap, vUV).rgb;
    }
#endif
    return baseCol;
}

#if defined(USE_LIGHTING) && defined(HAS_SHADOWS)
float sampleShadowTexture(int cascadeIndex, vec2 uv) {
    if (cascadeIndex == 0) {
        return texture(ShadowMap0, uv).r;
    }
    if (cascadeIndex == 1) {
        return texture(ShadowMap1, uv).r;
    }
    if (cascadeIndex == 2) {
        return texture(ShadowMap2, uv).r;
    }
    return texture(ShadowMap3, uv).r;
}

int selectCascade(float viewDepth) {
    int cascadeCount = int(clamp(sceneLight.shadowParams.w, 1.0, 4.0));
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewDepth <= sceneLight.cascadeSplits[i]) {
            return i;
        }
    }
    return cascadeCount - 1;
}

float viewDepthForWorldPos(vec3 worldPos) {
    return max(0.0, -(camera.view * vec4(worldPos, 1.0)).z);
}

bool isInsideShadowDistance(float viewDepth) {
    int cascadeCount = int(clamp(sceneLight.shadowParams.w, 1.0, 4.0));
    return viewDepth <= sceneLight.cascadeSplits[cascadeCount - 1];
}

vec3 cascadeDebugColor(vec3 worldPos) {
    float viewDepth = viewDepthForWorldPos(worldPos);
    if (!isInsideShadowDistance(viewDepth)) {
        return vec3(1.0);
    }
    int cascadeIndex = selectCascade(viewDepth);
    if (cascadeIndex == 0) {
        return vec3(0.95, 0.20, 0.18);
    }
    if (cascadeIndex == 1) {
        return vec3(0.18, 0.75, 0.25);
    }
    if (cascadeIndex == 2) {
        return vec3(0.20, 0.45, 1.0);
    }
    return vec3(0.95, 0.75, 0.15);
}

float sampleShadowCascade(int cascadeIndex, vec3 worldPos, vec3 normal, vec3 lightDir) {
    vec4 lightSpacePos =
        sceneLight.cascadeViewProj[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // Shadow pass uses Vulkan's negative-height viewport convention, so the
    // rendered depth texture is Y-flipped relative to clip-space UVs.
    projCoords.y = 1.0 - projCoords.y;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    float cascadeDepthRange = max(sceneLight.cascadeDepthRanges[cascadeIndex], 0.001);
    float worldBias = max(sceneLight.shadowParams.y, 0.0);
    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    float slopeBias = worldBias * (1.0 - ndotl) * 0.5;
    float depthBias = (worldBias + slopeBias) / cascadeDepthRange;
    vec2 texelSize = vec2(1.0 / max(sceneLight.shadowParams.x, 1.0));
    float visibility = 0.0;
    float sampleCount = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 sampleUv = projCoords.xy + vec2(x, y) * texelSize;
            if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
                sampleUv.y < 0.0 || sampleUv.y > 1.0) {
                continue;
            }
            float closestDepth = sampleShadowTexture(
                cascadeIndex, sampleUv);
            visibility += (projCoords.z - depthBias) <= closestDepth ? 1.0 : 0.0;
            sampleCount += 1.0;
        }
    }
    if (sampleCount <= 0.0) {
        return 1.0;
    }
    return visibility / sampleCount;
}

float sampleShadowMap(vec3 worldPos, vec3 normal, vec3 lightDir) {
    float viewDepth = viewDepthForWorldPos(worldPos);
    if (!isInsideShadowDistance(viewDepth)) {
        return 1.0;
    }

    int cascadeIndex = selectCascade(viewDepth);
    float visibility =
        sampleShadowCascade(cascadeIndex, worldPos, normal, lightDir);

    int cascadeCount = int(clamp(sceneLight.shadowParams.w, 1.0, 4.0));
    if (cascadeIndex >= cascadeCount - 1) {
        return visibility;
    }

    float splitEnd = sceneLight.cascadeSplits[cascadeIndex];
    float splitStart =
        cascadeIndex == 0 ? 0.0 : sceneLight.cascadeSplits[cascadeIndex - 1];
    float splitRange = max(splitEnd - splitStart, 0.001);
    float blendWidth = clamp(splitRange * 0.15, 0.25, 3.0);
    float blendStart = splitEnd - blendWidth;
    if (viewDepth <= blendStart) {
        return visibility;
    }

    float nextVisibility =
        sampleShadowCascade(cascadeIndex + 1, worldPos, normal, lightDir);
    float blend = smoothstep(blendStart, splitEnd, viewDepth);
    return mix(visibility, nextVisibility, blend);
}
#endif

#ifdef USE_LIGHTING
vec3 computeSmoothNormal() {
#ifdef USE_NORMAL_MAP
    mat3 tbn = vTBN;
    tbn[0] = normalize(tbn[0]);
    tbn[1] = normalize(tbn[1]);
    tbn[2] = normalize(tbn[2]);
    vec3 normal = tbn[2];
    if (material.enableNormal == 1) {
        vec3 normalSample = texture(normalMap, vUV).rgb * 2.0 - 1.0;
        normal = normalize(tbn * normalSample);
    }
    return normal;
#else
    return normalize(vWorldNormal);
#endif
}

vec3 computeFlatNormal() {
    vec3 fallback = normalize(vWorldNormal);
    vec3 flatNormal = cross(dFdx(vWorldPos), dFdy(vWorldPos));
    float len2 = dot(flatNormal, flatNormal);
    if (len2 < 1e-10) {
        return fallback;
    }
    flatNormal = flatNormal * inversesqrt(len2);
    if (dot(flatNormal, fallback) < 0.0) {
        flatNormal = -flatNormal;
    }
    return flatNormal;
}
#endif

void main() {
    vec3 baseCol = computeBaseColor();

#ifndef USE_LIGHTING
    outColor = vec4(baseCol, 1.0);
    return;
#else
#ifdef USE_FLAT_SHADING
    vec3 N = computeFlatNormal();
#else
    vec3 N = computeSmoothNormal();
#endif
    vec3 ambient = baseCol * material.ambientIntensity;
    vec3 finalColor = ambient;

    vec3 L = normalize(-sceneLight.dir.xyz);
    vec3 V = normalize(camera.eyePos - vWorldPos);
    float diff = max(dot(N, L), 0.0);
#ifdef HAS_SHADOWS
    if (material.debugShadowMode == 2) {
        outColor = vec4(cascadeDebugColor(vWorldPos), 1.0);
        return;
    }
    float shadowVisibility = sampleShadowMap(vWorldPos, N, L);
    if (material.debugShadowMode == 1) {
        outColor = vec4(vec3(shadowVisibility), 1.0);
        return;
    }
    float shadowStrength = clamp(sceneLight.shadowParams.z, 0.0, 1.0);
    float directVisibility = mix(1.0, shadowVisibility, shadowStrength);
#else
    float directVisibility = 1.0;
#endif
    vec3 diffuse = diff * sceneLight.color.rgb;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), material.shininess);
    vec3 specular = spec * sceneLight.color.rgb * material.specularIntensity;
    finalColor += ((baseCol * diffuse) + specular) * directVisibility;

    outColor = vec4(finalColor, 1.0);
#endif
}
