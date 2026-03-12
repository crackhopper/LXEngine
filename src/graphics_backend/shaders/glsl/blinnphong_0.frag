#version 450
layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 1, binding = 0) uniform MaterialUBO
{
    int enableLighting;
    int enableTexture;
    int enableNormalMap;
    int padding0;       // 填充

    vec3 baseColor;
    float shininess;
} material;

layout(set = 3, binding = 0) uniform sampler2D colorTex;
layout(set = 3, binding = 1) uniform sampler2D normalTex;

layout(set = 4, binding = 0) uniform LightUBO
{
    vec3 lightPos;
    vec3 lightColor;
} light;



layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vColor;
layout(location = 4) in mat3 TBN;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 albedo = material.baseColor * vColor;

    if(material.enableTexture == 1)
    {
        albedo *= texture(colorTex, vUV).rgb;
    }

    if(material.enableLighting == 0)
    {
        outColor = vec4(albedo,1.0);
        return;
    }

    vec3 N = normalize(vNormal);

    if(material.enableNormalMap == 1)
    {
        vec3 normalSample = texture(normalTex,vUV).xyz;
        normalSample = normalSample * 2.0 - 1.0;
        N = normalize(TBN * normalSample);
    }

    vec3 L = normalize(light.lightPos - vWorldPos);
    vec3 V = normalize(camera.eyePos - vWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N,L),0.0);
    float spec = pow(max(dot(N,H),0.0), material.shininess);

    vec3 ambient = 0.05 * albedo;

    vec3 diffuse = diff * albedo * light.lightColor;

    vec3 specular = spec * light.lightColor;

    vec3 color = ambient + diffuse + specular;

    outColor = vec4(color,1.0);
}