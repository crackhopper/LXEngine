#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec2 vUV;
layout(location = 2) in mat3 vTBN;

layout(push_constant) uniform ObjectPC {
    mat4 model;
    int  enableLighting;
    int  enableSkinning; // 通常在顶点着色器使用，但为了布局对齐也写在这里
    int  padding[2];
} object;

layout(set = 0, binding = 0) uniform LightUBO {
    vec4 dir;
    vec4 color;
} sceneLight;

layout(set = 2, binding = 0) uniform MaterialUBO {
    vec3 baseColor;          // 
    float shininess;         // 默认 12.0 ，控制高光半径/锐利度。越大越锐利。

    float specularIntensity; // 高光强度
    int enableAlbedo;        // 是否启用纹理
    int enableNormal;        // 是否启用法线贴图      
    int padding;     
} material;

layout(set = 2, binding = 1) uniform sampler2D albedoMap;
layout(set = 2, binding = 2) uniform sampler2D normalMap;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(location = 0) out vec4 outColor;

void main() {
    // 1. 重新归一化 TBN (修复线性插值导致的误差)
    mat3 tbn = vTBN;
    tbn[0] = normalize(tbn[0]);
    tbn[1] = normalize(tbn[1]);
    tbn[2] = normalize(tbn[2]);
    
    vec3 N = tbn[2];

    // 2. 获取最终法线 (法线贴图)
    if (material.enableNormal == 1) {
        vec3 normalSample = texture(normalMap, vUV).rgb * 2.0 - 1.0;
        N = normalize(tbn * normalSample);
    }

    // 3. 基础颜色
    vec3 baseCol = material.baseColor;
    if (material.enableAlbedo == 1) {
        baseCol *= texture(albedoMap, vUV).rgb;
    }

    // 4. 光照计算 (Blinn-Phong)
    vec3 ambient = baseCol * 0.1; // 基础环境光项
    vec3 finalColor = ambient;

    if (object.enableLighting == 1) {
        vec3 L = normalize(-sceneLight.dir.xyz);
        vec3 V = normalize(camera.eyePos - vWorldPos);
        
        // --- 漫反射 (Diffuse) ---
        float diff = max(dot(N, L), 0.0);
        vec3 diffuse = diff * sceneLight.color.rgb;

        // --- 高光 (Specular) ---
        // 使用强度系数代替开关，0.0 自动抵消计算结果
        vec3 H = normalize(L + V); 
        float spec = pow(max(dot(N, H), 0.0), material.shininess);
        vec3 specular = spec * sceneLight.color.rgb * material.specularIntensity;

        // 最终叠加：(物体色 * 漫反射) + 镜面反射
        finalColor += (baseCol * diffuse) + specular;
    }

    outColor = vec4(finalColor, 1.0);

    // 测试代码
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}