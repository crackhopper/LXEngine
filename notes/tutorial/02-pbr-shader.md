# 02 · PBR Shader

> 把 [01 章](01-pbr-theory.md) 的公式翻译成 GLSL。两个新文件放到 `shaders/glsl/`，shader 编译 CMake target `CompileShaders` 会自动 glob 并产出 SPIR-V。

## 文件位置

```
shaders/glsl/pbr_cube.vert        ← 本章产出
shaders/glsl/pbr_cube.frag        ← 本章产出
```

`shaders/CMakeLists.txt` 已经 `file(GLOB ...)` 匹配 `*.vert` / `*.frag`，新加的文件无需改 CMake，只需重新 configure 一次让 glob 刷新。

---

## 1. 顶点着色器

对照目标：把世界坐标 `vWorldPos` 和世界法线 `vNormal` 传给片元。不要切线空间（这个 tutorial 不使用 normal map）。

```glsl
// shaders/glsl/pbr_cube.vert
#version 450

// 与 PC_Draw (src/core/rhi/render_resource.hpp:83) 一致。
// 只访问 model；enableLighting/enableSkinning 占位以匹配 C++ 端布局。
layout(push_constant) uniform ObjectPC {
    mat4 model;
    int  enableLighting;
    int  enableSkinning;
    int  padding[2];
} object;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;              // 本教程不使用，但布局必须匹配
layout(location = 3) in vec4 inTangent;         // 同上
layout(location = 4) in ivec4 inBoneIDs;        // 同上
layout(location = 5) in vec4 inBoneWeights;     // 同上

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    gl_Position   = camera.proj * camera.view * worldPos;

    vWorldPos = worldPos.xyz;

    // 法线要用 model 的逆转置，处理非均匀缩放。
    mat3 normalMatrix = mat3(transpose(inverse(object.model)));
    vNormal = normalize(normalMatrix * inNormal);
}
```

!!! note "关于顶点输入布局"
    `VertexPosNormalUvBone` 定义在 `src/core/rhi/vertex_buffer.hpp:420`，有 6 个属性 location。顶点着色器**必须声明全部 6 个** `in` 位置才能和管线 layout 匹配，否则 pipeline 创建会在 validation layer 里报 location mismatch。用不到的属性直接声明再忽略即可。

---

## 2. 片元着色器

```glsl
// shaders/glsl/pbr_cube.frag
#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

// MaterialUBO — 名字必须是 "MaterialUBO"。
// MaterialInstance 构造时硬编码查 binding.name == "MaterialUBO"
// 来定位自己的 std140 字节 buffer。
// 见 notes/subsystems/material-system.md#注意事项
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec3  baseColor;     // offset 0,  size 12
    float metallic;      // offset 12, size 4   ← 故意塞进 vec3 后面的 4 字节槽
    float roughness;     // offset 16
    float ao;            // offset 20
    float _pad0;         // offset 24
    float _pad1;         // offset 28
} material;

layout(set = 2, binding = 0) uniform LightUBO {
    vec4 dir;            // xyz 方向，w 未用
    vec4 color;          // rgb 颜色/强度，a 未用
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
    vec3 N = normalize(vNormal);
    vec3 V = normalize(camera.eyePos - vWorldPos);
    vec3 L = normalize(-light.dir.xyz);
    vec3 H = normalize(V + L);

    vec3 albedo = material.baseColor;

    // F0: 非金属 4%，金属等于 albedo
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);

    // D · G · F
    float NDF = distributionGGX(N, H, material.roughness);
    float G   = geometrySmith(N, V, L, material.roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-4;
    vec3  specular    = numerator / denominator;

    // 能量守恒
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - material.metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3  Lo    = (kD * albedo / PI + specular) * light.color.rgb * NdotL;

    vec3 ambient = vec3(0.03) * albedo * material.ao;
    vec3 color   = ambient + Lo;

    // Tone mapping (Reinhard) + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
```

---

## 3. std140 里的那颗地雷

教程 `MaterialUBO` 故意长这样：

```glsl
vec3  baseColor;   // offset 0,  12 bytes
float metallic;    // offset 12, 4  bytes   ← 紧邻 vec3
float roughness;   // offset 16
float ao;          // offset 20
```

std140 规则里，`vec3` 占 12 字节但对齐到 16 字节；紧跟的 `float` 可以填入 `vec3` 末尾那 4 字节空槽。这**不是**糟糕的布局，而是 std140 的**正常行为**。对应地，`MaterialInstance::setVec3` 写 12 字节而不是 16 字节 —— 否则会 clobber `metallic`（详见 `notes/subsystems/material-system.md#反射驱动的-ubo-写入`）。

如果这里写成 `vec3 baseColor; vec3 something;`（两个 vec3 紧挨），std140 会在第一个后面插 4 字节 pad，第二个 vec3 会对齐到 offset 16。反射驱动的 setter 依赖 `ShaderReflector` 暴露的 `StructMemberInfo.offset`，对这种 pad **完全透明**，你只管 `setVec3(id, value)`。

---

## 4. Descriptor set 布局

三组 set：

| set | binding | 名字 | 来源 |
|-----|---------|------|------|
| 0 | 0 | `CameraUBO` | `Scene::getSceneLevelResources()` → `Camera::ubo` |
| 1 | 0 | `MaterialUBO` | `MaterialInstance` 的 std140 字节 buffer |
| 2 | 0 | `LightUBO` | `Scene::getSceneLevelResources()` → `DirectionalLight::ubo` |

本项目的 descriptor 绑定由 **反射驱动**，`cmd->bindResources` 按 `IRenderResource::getBindingName()` 去匹配 shader 的 `ShaderResourceBinding`，不按 set 号硬匹配。只要 shader 里的 UBO **名字**和 C++ 端资源 `getBindingName()` 返回的 `StringID` 一致就行：

- `CameraUBO` — 见 `src/core/scene/camera.hpp:32`，`getBindingName()` 返回 `"CameraUBO"`
- `LightUBO` — 见 `src/core/scene/light.hpp:58`，`getBindingName()` 返回 `"LightUBO"`
- `MaterialUBO` — 见 `src/core/asset/material.hpp` 的 `UboByteBufferResource`

所以上面那张 set/binding 表里的数字**你可以随便挑**，但名字不能变。

---

## 5. 下一步

shader 文件就位后，我们需要一个 loader 把它编成 `IShader`、构造 `MaterialTemplate` + `MaterialInstance`，并给 UBO 种子默认值。

→ [03-material-loader.md](03-material-loader.md)
