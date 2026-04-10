#version 450

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

// Light
layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

const float PI = 3.14159265359;

// ---------- PBR functions ----------

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) *
           geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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

    // Normal
    vec3 N = normalize(vNormal);

#ifdef HAS_NORMAL_MAP
    vec3 tangentNormal = texture(normalMap, vUV).rgb * 2.0 - 1.0;
    N = normalize(vTBN * tangentNormal);
#endif

    vec3 V = normalize(camera.eyePos - vWorldPos);
    vec3 L = normalize(-light.direction.xyz);
    vec3 H = normalize(V + L);

    // F0: reflectance at normal incidence
    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);

    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo.rgb / PI + specular) * light.color.rgb * NdotL;

    // Ambient
    vec3 ambient = vec3(0.03) * albedo.rgb * material.ao;

    vec3 color = ambient + Lo;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, albedo.a);
}
