#version 450

layout(location = 0) in vec3 vWorldNormal;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec3 surfaceColor;
    float mixAmount;
    vec4 accentColor;
    int mode;
} material;

layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

void main() {
    vec3 n = normalize(vWorldNormal);
    float ndotl = max(dot(n, normalize(-light.direction.xyz)), 0.0);
    float band = material.mode == 0 ? ndotl : step(0.5, ndotl);
    vec3 lit = material.surfaceColor * (0.25 + 0.75 * band) * light.color.rgb;
    vec3 color = mix(lit, material.accentColor.rgb, clamp(material.mixAmount, 0.0, 1.0));
    outColor = vec4(color, material.accentColor.a);
}
