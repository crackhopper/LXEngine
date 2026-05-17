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
    vec4 shadowParams;
} sceneLight;
layout(set = 0, binding = 1) uniform sampler2D ShadowMap0;
layout(set = 0, binding = 2) uniform sampler2D ShadowMap1;
layout(set = 0, binding = 3) uniform sampler2D ShadowMap2;
layout(set = 0, binding = 4) uniform sampler2D ShadowMap3;
#endif

layout(set = 2, binding = 0) uniform MaterialUBO {
    vec3 baseColor;
    float shininess;

    float specularIntensity;
    int enableAlbedo;
    int enableNormal;
    int padding;
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

#ifdef USE_LIGHTING
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

float sampleShadowMap(vec3 worldPos, vec3 normal, vec3 lightDir) {
    float viewDepth = abs((camera.view * vec4(worldPos, 1.0)).z);
    int cascadeIndex = selectCascade(viewDepth);
    vec4 lightSpacePos =
        sceneLight.cascadeViewProj[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    float baseBias = max(sceneLight.shadowParams.y, 0.0005);
    float slopeBias = max(baseBias * (1.0 - dot(normal, lightDir)), baseBias);
    vec2 texelSize = vec2(1.0 / max(sceneLight.shadowParams.x, 1.0));
    float visibility = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closestDepth = sampleShadowTexture(
                cascadeIndex, projCoords.xy + vec2(x, y) * texelSize);
            visibility += (projCoords.z - slopeBias) <= closestDepth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}
#endif

void main() {
    vec3 baseCol = computeBaseColor();

#ifndef USE_LIGHTING
    outColor = vec4(baseCol, 1.0);
    return;
#else
#ifdef USE_NORMAL_MAP
    mat3 tbn = vTBN;
    tbn[0] = normalize(tbn[0]);
    tbn[1] = normalize(tbn[1]);
    tbn[2] = normalize(tbn[2]);
    vec3 N = tbn[2];
    if (material.enableNormal == 1) {
        vec3 normalSample = texture(normalMap, vUV).rgb * 2.0 - 1.0;
        N = normalize(tbn * normalSample);
    }
#else
    vec3 N = normalize(vWorldNormal);
#endif
    vec3 ambient = baseCol * 0.1;
    vec3 finalColor = ambient;

    vec3 L = normalize(-sceneLight.dir.xyz);
    vec3 V = normalize(camera.eyePos - vWorldPos);
    float diff = max(dot(N, L), 0.0);
    float shadowVisibility = sampleShadowMap(vWorldPos, N, L);
    float shadowStrength = clamp(sceneLight.shadowParams.z, 0.0, 1.0);
    float directVisibility = mix(1.0, shadowVisibility, shadowStrength);
    vec3 diffuse = diff * sceneLight.color.rgb;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), material.shininess);
    vec3 specular = spec * sceneLight.color.rgb * material.specularIntensity;
    finalColor += ((baseCol * diffuse) + specular) * directVisibility;

    outColor = vec4(finalColor, 1.0);
#endif
}
