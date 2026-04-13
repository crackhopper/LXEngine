# Material System

> 材质系统的唯一真相是 `MaterialInstance`：基于 shader 反射自动管理 UBO 字节 buffer 的模板-实例架构。REQ-005 之后 `DrawMaterial` 与硬编码的 `BlinnPhongMaterialUBO` 已**彻底删除**。
>
> 权威 spec: `openspec/specs/material-system/spec.md`
> 深度设计: `docs/design/MaterialSystem.md`

## 核心抽象

`src/core/resources/material.hpp`:

- **`IMaterial`** (`:157`) — 抽象接口
  - `getDescriptorResources() → vector<IRenderResourcePtr>`
  - `getShaderInfo() → IShaderPtr`
  - `getPassFlag() → ResourcePassFlag`
  - `getRenderState() → RenderState`
  - `getRenderSignature(pass) → StringID`（REQ-007 之后）
- **`RenderState`** (`:68`) — 固定管线状态值类型；`getRenderSignature()` 贡献 pipeline 身份
- **`RenderPassEntry`** (`:114`) — 一个 pass 的配置：`{renderState, shaderSet, bindingCache}`
- **`MaterialTemplate`** (`:175`) — 材质蓝图
  - 构造必须传入 `IShaderPtr`（`create(name, shader)` 工厂）
  - `setPass(StringID pass, RenderPassEntry entry)` — per-pass 配置
  - `buildBindingCache()` — 从 shader 反射建 `StringID → ShaderResourceBinding` 全局查表
  - `findBinding(StringID id) → optional<ref<const ShaderResourceBinding>>`
- **`UboByteBufferResource`** (`:247`) — `IRenderResource` 的一个实现，对 `vector<uint8_t>` 做非拥有包装。`MaterialInstance` 用它把自己的 std140 buffer 暴露给 descriptor sync 路径
- **`MaterialInstance`** (`:272`) — **唯一**的 `IMaterial` 实现
  - `create(template, passFlag)` 工厂
  - **非拷贝非移动**（因为 `m_uboResource` 指向 `m_uboBuffer`，移动会悬垂）
  - 构造时扫描反射 binding 找 `"MaterialUBO"`，分配 std140 字节 buffer
  - setter: `setVec4 / setVec3 / setFloat / setInt / setTexture`
  - `updateUBO()` — 标 dirty 让 backend 同步

## 典型用法

```cpp
#include "infra/loaders/blinnphong_material_loader.hpp"
#include "core/scene/pass.hpp"

using namespace LX_core;

// 一行创建 + 种子默认值
auto material = LX_infra::loadBlinnPhongMaterial();  // MaterialInstance::Ptr

// 运行期修改
material->setVec3(StringID("baseColor"), Vec3f{1.0f, 0.25f, 0.5f});
material->setFloat(StringID("shininess"), 32.0f);
material->setInt(StringID("enableNormal"), 0);
material->updateUBO();                                // 标 dirty

// 绑到 renderable
auto renderable = std::make_shared<RenderableSubMesh>(mesh, material, skeletonOptional);
```

loader 内部（`src/infra/loaders/blinnphong_material_loader.cpp`）:

```cpp
// 1. 编译 + 反射
auto compiled = ShaderCompiler::compileProgram(vert, frag, {});
auto bindings = ShaderReflector::reflect(compiled.stages);
auto shader   = std::make_shared<ShaderImpl>(std::move(compiled.stages),
                                             bindings, "blinnphong_0");

// 2. 模板
auto tmpl = MaterialTemplate::create("blinnphong_0", shader);

// 3. Pass 配置
RenderPassEntry entry;
entry.shaderSet   = ShaderProgramSet{"blinnphong_0", {}};
entry.renderState = RenderState{};
entry.buildCache();
tmpl->setPass(Pass_Forward, std::move(entry));
tmpl->buildBindingCache();

// 4. Instance + 种子默认
auto mat = MaterialInstance::create(tmpl, ResourcePassFlag::Forward);
mat->setVec3(StringID("baseColor"),         Vec3f{0.8f, 0.8f, 0.8f});
mat->setFloat(StringID("shininess"),        12.0f);
mat->setFloat(StringID("specularIntensity"), 1.0f);
mat->setInt  (StringID("enableAlbedo"),      0);
mat->setInt  (StringID("enableNormal"),      0);
mat->updateUBO();
```

## 反射驱动的 UBO 写入

REQ-004 起 `ShaderReflector` 在 `ShaderResourceBinding.members` 里暴露 std140 member 布局。`MaterialInstance` 把这些信息用于 setter：

```cpp
void MaterialInstance::setVec3(StringID id, const Vec3f &value) {
    writeUboMember(id, &value, sizeof(float) * 3, ShaderPropertyType::Vec3);
}

// writeUboMember 的核心逻辑：
// - 在 m_uboBinding->members 里线性查 StringID(m.name) == id
// - 断言 m.type == expected（例如 Vec3）
// - memcpy(m_uboBuffer.data() + m.offset, src, nbytes)
// - m_uboDirty = true
```

**std140 pack 关键点**: `vec3` 写 **12 字节**而不是 16，否则会 clobber 紧邻的 `float` 成员（例如 `vec3 baseColor; float shininess;` 里 shininess 在 offset 12，写 16 会覆盖它）。这是 REQ-005 设计时 `setVec3` 写 12 字节的唯一原因。

## 调用关系

```
loader (infra)
  │ 编译 shader + 反射
  ▼
ShaderImpl
  │
  ▼
MaterialTemplate::create(name, shader)
  │
  │ setPass(Pass_Forward, entry)
  │ buildBindingCache()
  ▼
MaterialInstance::create(tmpl)
  │ 构造时扫反射找 "MaterialUBO" binding
  │ 分配 m_uboBuffer
  │ 构造 m_uboResource（UboByteBufferResource 包装 m_uboBuffer）
  ▼
Scene 通过 RenderableSubMesh 持有 MaterialPtr (= MaterialInstance::Ptr)
  │
  ▼
Scene::buildRenderingItem(pass) 调用:
  - material->getDescriptorResources() → [m_uboResource, tex1, tex2, ...]
  - material->getRenderSignature(pass) → StringID
  │
  ▼
RenderingItem
```

## 注意事项

- **`MaterialUBO` 必须叫这个名字**: `MaterialInstance` 构造时硬编码查找 `binding.name == "MaterialUBO"` 来定位自己的 UBO。shader 里把 material uniform 块命名为其他名字会导致 `m_uboBinding == nullptr`，后续 setter 全部 assert fail。项目里的 scene 级 UBO (`LightUBO` / `CameraUBO` / `Bones`) 是**故意**不叫 MaterialUBO 的，它们属于其他 owner。
- **非拷贝非移动**: `MaterialInstance` 拥有 `m_uboBuffer`（`vector<uint8_t>`）和 `m_uboResource`（持有指向 `m_uboBuffer` 的原始指针）。移动会让原始指针悬垂，所以类显式 `delete` 了拷贝/移动构造与赋值。`create()` 返回 `shared_ptr`。
- **Texture 类型**: `setTexture` 接受 **`CombinedTextureSamplerPtr`**，不接受裸 `TexturePtr` —— 因为 `CombinedTextureSampler` 才是 `IRenderResource`。把 texture 和 sampler 成对包装是 backend 描述符写入的前提。
- **`IMaterial::getShaderProgramSet()` 已废弃**: REQ-007 后 shader 身份通过 `getRenderSignature(pass)` 透传到 `PipelineKey`，没人再需要从 material 拿 `ShaderProgramSet` 了。该虚方法已从接口删除。
- **Pass key 已是 `StringID`**: `MaterialTemplate::setPass(Pass_Forward, ...)`（REQ-007 之后）。老代码里如果看到 `setPass("Forward", ...)` 用字符串字面量的隐式构造也可以，效果相同（`StringID` 从 `const char*` 隐式构造）。

## 测试

- `src/test/integration/test_material_instance.cpp` — 非 GPU 测试：UBO 分配 / vec3 不 clobber shininess / setter 类型校验 / descriptor 资源稳定身份 / loader 端到端
- `src/test/integration/test_pipeline_identity.cpp` — 覆盖材质对 `PipelineKey` 的贡献
- `src/test/integration/test_pipeline_build_info.cpp` — `PipelineBuildInfo::fromRenderingItem` 从材质读字段的路径

## 延伸阅读

- `openspec/specs/material-system/spec.md` — 9 条 ADDED requirement（sole impl / shader required / UBO allocation / reflection setters / texture bindings / descriptor order / GPU sync / UboByteBufferResource / loader）
- `docs/design/MaterialSystem.md` — Template-Instance 架构的设计权衡与 per-pass binding cache
- 归档: `openspec/changes/archive/2026-04-13-unify-material-system/` — REQ-005 的完整实施记录（9 条 requirement + 32 个 scenario）
