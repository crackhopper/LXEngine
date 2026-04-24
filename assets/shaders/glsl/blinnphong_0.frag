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
} sceneLight;
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
    vec3 diffuse = diff * sceneLight.color.rgb;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), material.shininess);
    vec3 specular = spec * sceneLight.color.rgb * material.specularIntensity;
    finalColor += (baseCol * diffuse) + specular;

    outColor = vec4(finalColor, 1.0);
#endif
}
