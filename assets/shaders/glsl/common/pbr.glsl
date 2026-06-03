#ifndef LX_COMMON_PBR_GLSL
#define LX_COMMON_PBR_GLSL

const float LX_PBR_PI = 3.14159265359;

struct LxPbrDirectInput {
  vec3 baseColor;
  vec3 normal;
  vec3 viewDir;
  vec3 lightDir;
  vec3 lightColor;
  float metallic;
  float roughness;
  float ao;
  vec3 emissive;
};

float lxDistributionGGX(vec3 N, vec3 H, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float denom = NdotH2 * (a2 - 1.0) + 1.0;
  denom = LX_PBR_PI * denom * denom;
  return a2 / max(denom, 0.0001);
}

float lxGeometrySchlickGGX(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return NdotV / (NdotV * (1.0 - k) + k);
}

float lxGeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  return lxGeometrySchlickGGX(NdotV, roughness) *
         lxGeometrySchlickGGX(NdotL, roughness);
}

vec3 lxFresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 lxFresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
                  pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 lxPbrF0(vec3 baseColor, float metallic) {
  return mix(vec3(0.04), baseColor, clamp(metallic, 0.0, 1.0));
}

vec3 lxPbrDirectLight(LxPbrDirectInput pbr) {
  vec3 N = normalize(pbr.normal);
  vec3 V = normalize(pbr.viewDir);
  vec3 L = normalize(pbr.lightDir);
  vec3 H = normalize(V + L);
  float roughness = clamp(pbr.roughness, 0.04, 1.0);
  float metallic = clamp(pbr.metallic, 0.0, 1.0);

  vec3 F0 = lxPbrF0(pbr.baseColor, metallic);
  float NDF = lxDistributionGGX(N, H, roughness);
  float G = lxGeometrySmith(N, V, L, roughness);
  vec3 F = lxFresnelSchlick(max(dot(H, V), 0.0), F0);

  vec3 numerator = NDF * G * F;
  float denominator =
      4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
  vec3 specular = numerator / denominator;

  vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
  float NdotL = max(dot(N, L), 0.0);
  return (kD * pbr.baseColor / LX_PBR_PI + specular) *
         pbr.lightColor * NdotL;
}

vec3 lxPbrFallbackAmbient(LxPbrDirectInput pbr) {
  return vec3(0.03) * pbr.baseColor * clamp(pbr.ao, 0.0, 1.0);
}

vec3 lxPbrEmissive(LxPbrDirectInput pbr) {
  return pbr.emissive;
}

#endif
