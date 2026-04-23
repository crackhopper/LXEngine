# 02 · PBR Shader

> 把 [01 章](01-pbr-theory.md) 的公式翻译成 GLSL，并先和当前仓库里已经存在的 `shaders/glsl/pbr.vert` / `pbr.frag` 对齐。这里的重点不是“凭空设计一套接口”，而是确认它如何接到现在这套引擎封装上。

## 文件位置

```text
shaders/glsl/pbr.vert
shaders/glsl/pbr.frag
```

`shaders/CMakeLists.txt` 已经 `file(GLOB ...)` 匹配 `*.vert` / `*.frag`，所以 shader 文件本身已经会被 `CompileShaders` 编进 SPIR-V。当前缺的不是 shader 编译链路，而是 PBR shader 对应的 loader 和示例入口。

---

## 1. 当前 shader 与现有 ABI 的对齐点

当前 `pbr.vert` / `pbr.frag` 已经采用了和引擎一致的几个关键约定：

- push constant 块名仍是 `ObjectPC`
- 这个 GLSL block 目前只包含一个 `mat4 model`，对应 C++ 侧的 `PerDrawLayout`
- 相机 UBO 名字是 `CameraUBO`
- 材质 UBO 名字是 `MaterialUBO`
- 灯光 UBO 名字是 `LightUBO`
- fragment 输出是单个 `layout(location = 0) out vec4 outColor`

这几点分别对应：

- `src/core/rhi/gpu_resource.hpp` 中的 `PerDrawLayout = PerDrawLayoutBase`
- `src/core/scene/camera.hpp` 中 `CameraData::getBindingName()`
- `src/core/asset/material_instance.cpp` / `material_instance.hpp` 中 `MaterialUBO` 约定
- `src/core/scene/light.hpp` 中 `DirectionalLightData::getBindingName()`

换句话说，当前 PBR shader 的问题已经不是“接口名不对”，而是“还没被 loader / example 接起来”。

## 2. 顶点着色器

当前仓库里的 `pbr.vert` 核心长这样：

```glsl
// shaders/glsl/pbr.vert
#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
} object;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;

#ifdef HAS_NORMAL_MAP
layout(location = 3) out mat3 vTBN;
#endif

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;
    vWorldPos = worldPos.xyz;
    vUV = inUV;

    mat3 normalMatrix = mat3(transpose(inverse(object.model)));
    vNormal = normalize(normalMatrix * inNormal);

#ifdef HAS_NORMAL_MAP
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    vec3 N = vNormal;
    vec3 B = normalize(cross(N, T) * inTangent.w);
    vTBN = mat3(T, B, N);
#endif
}
```

这里需要注意一件事：当前项目真正成熟的“变体驱动 shader 契约”写在 `blinnphong_0.vert/.frag` 里，PBR shader 还只是原型。也就是说：

- `blinnphong_0.vert` 会根据 `USE_UV` / `USE_NORMAL_MAP` / `USE_SKINNING` 缩放输入布局
- `pbr.vert` 目前直接固定声明了 `inPosition` / `inNormal` / `inUV` / `inTangent`
- 如果后续要把 PBR 正式接到现有 loader 体系里，建议沿用 Blinn-Phong 那套“变体决定 shader 输入/输出”的结构，而不是继续扩一套并行约定

---

## 3. 片元着色器

```glsl
// shaders/glsl/pbr.frag
#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float ao;
    float padding;
} material;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;

#ifdef HAS_NORMAL_MAP
layout(set = 1, binding = 2) uniform sampler2D normalMap;
#endif

#ifdef HAS_METALLIC_ROUGHNESS
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
#endif

layout(set = 2, binding = 0) uniform LightUBO {
    vec4 direction;
    vec4 color;
} light;

const float PI = 3.14159265359;

// ---------- BRDF building blocks ----------

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a     = roughness * roughness;
    float a2    = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d     = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-4);
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

// ---------- main ----------

void main() {
    vec4 albedo = texture(albedoMap, vUV) * material.baseColorFactor;
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;

#ifdef HAS_METALLIC_ROUGHNESS
    vec4 mr = texture(metallicRoughnessMap, vUV);
    metallic *= mr.b;
    roughness *= mr.g;
#endif

    vec3 N = normalize(vNormal);
#ifdef HAS_NORMAL_MAP
    vec3 tangentNormal = texture(normalMap, vUV).rgb * 2.0 - 1.0;
    N = normalize(vTBN * tangentNormal);
#endif

    vec3 V = normalize(camera.eyePos - vWorldPos);
    vec3 L = normalize(-light.direction.xyz);
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo.rgb / PI + specular) * light.color.rgb * NdotL;
    vec3 ambient = vec3(0.03) * albedo.rgb * material.ao;
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, albedo.a);
}
```

---

## 4. 当前版本需要特别注意的两点

- `MaterialUBO` 现在只是一个普通的 material-owned buffer binding 名，不再是唯一合法名字；真正不能随便改的是系统保留名 `CameraUBO` / `LightUBO` / `Bones`
- 当前 `pbr.frag` 已经不是“纯 UBO 驱动”，它默认还依赖 `albedoMap`，并可选依赖 `normalMap`、`metallicRoughnessMap`

这意味着它和教程 01 章里的“最小 PBR 心智模型”之间有一层现实差异：

- 01 章讲的是“最小无纹理 PBR”
- 当前 `pbr.frag` 写的是“以纹理为中心的 PBR shader 原型”
- 如果你的目标是先做一个**无纹理、纯参数驱动**的 PBR 立方体，那么在真正接 loader 之前，最好先把 `pbr.frag` 收敛成只依赖 `MaterialUBO` 的版本

反过来说，如果你要沿用当前这份 `pbr.frag`，那 03 章的 PBR loader 就不能只做 `setFloat/setVec4`，还需要把纹理资源也挂到材质实例上。

---

## 5. Descriptor set 布局

三组 set：

| set | binding | 名字 | 来源 |
|-----|---------|------|------|
| 0 | 0 | `CameraUBO` | `Scene::getSceneLevelResources()` → `Camera::ubo` (`CameraData`) |
| 1 | 0 | `MaterialUBO` | `MaterialInstance` 的 std140 字节 buffer |
| 1 | 1 | `albedoMap` | `MaterialInstance::setTexture(...)` |
| 2 | 0 | `LightUBO` | `Scene::getSceneLevelResources()` → `DirectionalLight::ubo` |

本项目的 descriptor 绑定由 **反射驱动**，后端真正依赖的是 binding 名和反射结果，而不是你在文档里手抄的表格。只要 shader 里的 UBO / 采样器名字与 C++ 侧资源能对上，set/binding 就能通过反射找到。

- `CameraUBO` — 见 `src/core/scene/camera.hpp`，`CameraData::getBindingName()` 返回 `"CameraUBO"`
- `LightUBO` — 见 `src/core/scene/light.hpp:58`，`getBindingName()` 返回 `"LightUBO"`
- `MaterialUBO` — 当前 shader 恰好这样命名；如果改成别的非保留名字，`MaterialInstance` 仍能按反射识别它

真正不能乱改的是系统保留名字；材质自有的 buffer / texture binding 只要和 shader 反射保持一致即可。纹理这一层名字仍然最好直接沿用 shader 里的 `albedoMap` / `normalMap` / `metallicRoughnessMap`，这样最省事。

---

## 6. 下一步

shader 已经在仓库里，下一步不是“再写一遍 shader”，而是给它补一个真正接入当前材质系统的 loader。

→ [03-material-loader.md](03-material-loader.md)
