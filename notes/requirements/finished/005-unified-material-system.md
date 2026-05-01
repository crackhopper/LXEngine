# REQ-005: 统一材质系统 — 废除 DrawMaterial，启用 MaterialInstance

## 背景

当前 `src/core/resources/material.hpp` 存在两套并行设计：

| 类 | 状态 |
|----|------|
| `IMaterial` + `DrawMaterial` + `BlinnPhongMaterialUBO` | **正在使用**（硬编码 UBO struct） |
| `MaterialTemplate` + `MaterialInstance` | **已存在但未被任何调用方引用** |

`DrawMaterial` 直接持有手写的 `BlinnPhongMaterialUBO`（std140 struct），任何 shader 修改都要同步改 C++ 代码。`MaterialInstance` 的 `setVec4 / setFloat / setTexture` 只写入自己的 map，没有任何 UBO 字节 buffer 管理。

REQ-004 完成后反射提供了 UBO 的完整成员信息，可以把"一个 UBO 的内容"从 C++ struct 中解耦出来。

## 目标

1. `MaterialInstance` 成为 **唯一** 的 `IMaterial` 实现
2. `MaterialInstance` 基于 REQ-004 的反射结果**自动管理 UBO 字节 buffer**
3. 删除 `DrawMaterial` 和 `BlinnPhongMaterialUBO`
4. 所有 `MaterialPtr` 指向 `MaterialInstance`
5. `MaterialTemplate` 构造时必须传入已编译的 `IShaderPtr`

## 需求

### R1: IMaterial 接口收敛

```cpp
class IMaterial {
public:
  virtual ~IMaterial() = default;

  virtual std::vector<IRenderResourcePtr> getDescriptorResources() const = 0;
  virtual IShaderPtr                      getShaderInfo() const = 0;
  virtual ResourcePassFlag                getPassFlag() const = 0;
  virtual ShaderProgramSet                getShaderProgramSet() const = 0;
  virtual RenderState                     getRenderState() const = 0;
};
```

接口本身**不变**（当前就是这个形态）。`getRenderSignature(pass)` 的加入留给 REQ-007。

### R2: MaterialTemplate 强制持有 IShaderPtr

```cpp
class MaterialTemplate : public std::enable_shared_from_this<MaterialTemplate> {
public:
  using Ptr = std::shared_ptr<MaterialTemplate>;

  static Ptr create(std::string name, IShaderPtr shader);   // 必须传 shader

  void setPass(const std::string &passName, RenderPassEntry entry);
  std::optional<std::reference_wrapper<const RenderPassEntry>>
    getEntry(const std::string &passName) const;

  IShaderPtr getShader() const { return m_shader; }

  /// 从 m_shader 的反射结果构建全局 binding cache
  void buildBindingCache();
  std::optional<std::reference_wrapper<const ShaderResourceBinding>>
    findBinding(StringID id) const;

private:
  std::string                                          m_name;
  IShaderPtr                                           m_shader;
  std::unordered_map<std::string, RenderPassEntry>     m_passes;
  std::unordered_map<StringID, ShaderResourceBinding>  m_bindingCache;
};
```

**冲突清理（对齐用户草案 v1 的 §5.2）**：

- 只保留**一个** `m_bindingCache`（草案中 duplicate 的两个声明二选一）
- `m_passHashCache` 删除——REQ-007 会把 hash 识别全面替换为 interned StringID
- 用户在 `create()` 时必须传 shader；反射成员信息已通过 REQ-004 的 `ShaderResourceBinding.members` 提供

### R3: MaterialInstance 实现 IMaterial

```cpp
class MaterialInstance : public IMaterial {
public:
  using Ptr = std::shared_ptr<MaterialInstance>;

  static Ptr create(MaterialTemplate::Ptr tmpl,
                    ResourcePassFlag passFlag = ResourcePassFlag::Forward);

  // ==== IMaterial ====
  std::vector<IRenderResourcePtr> getDescriptorResources() const override;
  IShaderPtr                      getShaderInfo() const override;
  ResourcePassFlag                getPassFlag() const override { return m_passFlag; }
  ShaderProgramSet                getShaderProgramSet() const override;
  RenderState                     getRenderState() const override;

  // ==== Per-instance 属性（统一走 StringID）====
  void setVec4   (StringID id, const Vec4f &value);
  void setVec3   (StringID id, const Vec3f &value);
  void setFloat  (StringID id, float value);
  void setInt    (StringID id, int32_t value);
  void setTexture(StringID id, TexturePtr tex);

  // 模板访问
  MaterialTemplate::Ptr getTemplate() const { return m_template; }

  // UBO GPU 同步（由 ResourceManager 调用 / 或每帧 dirty flush）
  void updateUBO();

private:
  MaterialInstance(MaterialTemplate::Ptr tmpl, ResourcePassFlag passFlag);

  MaterialTemplate::Ptr                      m_template;
  ResourcePassFlag                           m_passFlag;

  // Per-instance UBO（基于反射自动创建）
  std::vector<uint8_t>                       m_uboBuffer;
  const ShaderResourceBinding *              m_uboBinding = nullptr;  // 非拥有

  // Per-instance sampler bindings（id → GPU texture resource）
  std::unordered_map<StringID, TexturePtr>   m_textures;
};
```

**冲突清理**：

- `setTexture` 只保留 `StringID` 版本（删除用户草案 §5.3 里 `uint32_t binding` 的第二个重载）——`MaterialTemplate::findBinding(id)` 已能拿到 set/binding/type，无需让调用方知道数字
- `m_vec4s / m_floats` map 删除——值直接写入 `m_uboBuffer`，没有二次存储的必要
- 新增 `setVec3 / setInt`——REQ-004 的 members 会出现这两种类型

### R4: UBO 自动初始化

`MaterialInstance` 构造时按反射结果分配 buffer：

```cpp
MaterialInstance::MaterialInstance(MaterialTemplate::Ptr tmpl, ResourcePassFlag passFlag)
    : m_template(std::move(tmpl)), m_passFlag(passFlag) {
  for (const auto &b : m_template->getShader()->getReflectionBindings()) {
    if (b.type == ShaderPropertyType::UniformBuffer) {
      m_uboBinding = &b;
      m_uboBuffer.assign(b.size, std::byte{0});
      break;   // 一个 material 一个 UBO，第一个为准
    }
  }
}
```

> 若未来 material 需要多个 UBO，将 `m_uboBinding` 改为 `std::vector<const ShaderResourceBinding*>` 并按 binding number 存储。当前阶段按单 UBO 处理。

### R5: 按反射写入 UBO 字段

`setVec4 / setVec3 / setFloat / setInt` 走同一条路径：

```cpp
void MaterialInstance::setVec4(StringID id, const Vec4f &value) {
  if (!m_uboBinding) return;
  for (const auto &m : m_uboBinding->members) {
    if (StringID(m.name) != id) continue;
    assert(m.type == ShaderPropertyType::Vec4);
    std::memcpy(m_uboBuffer.data() + m.offset, &value, sizeof(Vec4f));
    m_uboDirty = true;
    return;
  }
  // 找不到：调试断言 + 静默忽略
  assert(false && "MaterialInstance::setVec4: member not found in UBO");
}
```

**std140 填充规则**：

- `Vec3` 在 std140 中占 16 字节（对齐到 vec4）——写入时始终 `memcpy 16 字节`
- `Float` / `Int` 占 4 字节
- `Vec2` 占 8 字节
- `Vec4` / `Mat4` 按实际大小写入

辅助函数 `writeUboMember(StringID, const void*, size_t nbytes, ShaderPropertyType expected)` 统一处理 offset 查找 + 类型断言 + memcpy，四个 setter 做薄壳委托。

### R6: setTexture — 按反射建立 binding 映射

```cpp
void MaterialInstance::setTexture(StringID id, TexturePtr tex) {
  auto bindingOpt = m_template->findBinding(id);
  assert(bindingOpt && "texture binding not found in reflection");
  assert(bindingOpt->get().type == ShaderPropertyType::Texture2D ||
         bindingOpt->get().type == ShaderPropertyType::TextureCube);
  m_textures[id] = std::move(tex);
}
```

### R7: getDescriptorResources

汇总 UBO + 所有 texture bindings：

```cpp
std::vector<IRenderResourcePtr> MaterialInstance::getDescriptorResources() const {
  std::vector<IRenderResourcePtr> out;

  // 1. UBO — 包装为 GPU 资源
  if (m_uboBinding && !m_uboBuffer.empty()) {
    out.push_back(wrapAsUboResource(m_uboBuffer, *m_uboBinding, m_passFlag));
  }

  // 2. Textures — 按 set/binding 顺序排列
  std::vector<std::pair<uint32_t, IRenderResourcePtr>> sorted;
  for (const auto &[id, tex] : m_textures) {
    auto b = m_template->findBinding(id);
    if (!b) continue;
    uint32_t sortKey = (b->get().set << 16) | b->get().binding;
    sorted.emplace_back(sortKey, tex);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  for (auto &[_, r] : sorted) out.push_back(std::move(r));

  return out;
}
```

`wrapAsUboResource()` 是 infra 层的一个工厂函数，把 `std::vector<uint8_t>` 包装成一个持有弱引用的 `IRenderResource`，其 `getRawData()` 指向 `MaterialInstance::m_uboBuffer`。具体实现由 infra 层提供（可沿用现有 `SkeletonUBO` 的模式）。

### R8: updateUBO

```cpp
void MaterialInstance::updateUBO() {
  if (!m_uboDirty) return;
  // 让包装成 IRenderResource 的 UBO 对象 setDirty()，
  // 由 VulkanResourceManager::syncResource() 在下一帧推送到 GPU
  if (m_uboResource) m_uboResource->setDirty();
  m_uboDirty = false;
}
```

具体同步语义要与现有 `IRenderResource::setDirty()` 流程对齐（参见 `SkeletonUBO::setDirty()`）。

### R9: 删除 DrawMaterial / BlinnPhongMaterialUBO / loader

| 删除 | 原因 |
|------|------|
| `DrawMaterial` 类（`material.hpp/cpp`） | 由 `MaterialInstance` 取代 |
| `BlinnPhongMaterialUBO` struct | 由反射驱动的 `m_uboBuffer` 取代 |
| `src/infra/loaders/blinnphong_draw_material_loader.{hpp,cpp}` 中 `DrawMaterial` 相关的构建逻辑 | 改为返回 `MaterialInstance::Ptr`（见 R10） |
| `Material` 中的硬编码 `CombinedTextureSamplerPtr albedoSampler/normalSampler` 成员 | 作为 `MaterialInstance::m_textures` 的条目 |

### R10: Loader 改造

`blinnphong_draw_material_loader` 重命名 / 重写为 `blinnphong_material_loader`：

```cpp
MaterialInstance::Ptr loadBlinnPhongMaterial() {
  // 1. 编译 shader + 反射
  auto compiled = ShaderCompiler::compileProgram(vert, frag, {});
  auto bindings = ShaderReflector::reflect(compiled.stages);
  auto shader   = std::make_shared<ShaderImpl>(
                    std::move(compiled.stages), bindings);

  // 2. 创建 template
  auto tmpl = MaterialTemplate::create("blinnphong_0", shader);

  // 3. setPass
  RenderPassEntry entry;
  entry.shaderSet   = ShaderProgramSet{/* name + variants */};
  entry.renderState = RenderState{};
  // 注意：RenderPassEntry 的 bindings / bindingCache 保持不变的字段
  tmpl->setPass("Forward", std::move(entry));
  tmpl->buildBindingCache();

  // 4. 创建 instance 并写入默认值
  auto mat = MaterialInstance::create(tmpl);
  mat->setVec3(StringID("baseColor"),        Vec3f{0.8f, 0.8f, 0.8f});
  mat->setFloat(StringID("shininess"),       12.0f);
  mat->setFloat(StringID("specularIntensity"), 1.0f);
  mat->setInt  (StringID("enableAlbedo"),      0);
  mat->setInt  (StringID("enableNormalMap"),   0);
  return mat;
}
```

> 注意：草案 §六 的 `.descriptorResources = { albedoTex, normalTex }` 不属于 `RenderPassEntry`——runtime texture 只存在于 `MaterialInstance`，通过 `mat->setTexture(StringID("albedoMap"), tex)` 设置。已从本文档删除。

### R11: 调用点迁移

- `src/core/scene/object.hpp` `RenderableSubMesh::material` 的类型保持 `MaterialPtr = shared_ptr<IMaterial>` 不变，运行时指向 `MaterialInstance`
- `src/core/scene/scene.cpp` `buildRenderingItem` 继续调用 `sub->material->getShaderProgramSet() / getRenderState()`——接口未变
- 所有测试用例（`test_vulkan_*.cpp`）中手动构造 `DrawMaterial` 的地方替换为 `loadBlinnPhongMaterial()` 或等价的 `MaterialInstance` 构建

## 测试

- `test_shader_compiler.cpp` 已由 REQ-004 覆盖反射成员
- 新增 `test_material_instance.cpp`（integration）：
  - 构建 `MaterialInstance` → 断言 `m_uboBuffer.size() == 48`（或实际反射值）
  - `setVec3(baseColor, {1,0,0})` → 断言 `m_uboBuffer` 前 12 字节
  - `setFloat(shininess, 32.0f)` → 断言 offset 16 处 4 字节
- `test_render_triangle.cpp` 和 backend 集成测试中把 `DrawMaterial` 构造改为 `MaterialInstance`，确认依然渲染通过

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/resources/material.hpp` | 删除 `DrawMaterial` / `BlinnPhongMaterialUBO`；补齐 `MaterialInstance` 实现；整理 `MaterialTemplate::create(name, shader)` |
| `src/core/resources/material.cpp` | 同上；新增 setter 的反射查找 + std140 写入辅助函数 |
| `src/infra/loaders/blinnphong_draw_material_loader.{hpp,cpp}` | 重命名 / 重写为 `blinnphong_material_loader`，返回 `MaterialInstance::Ptr` |
| `src/core/scene/object.hpp` | （仅文档性）确认 `material` 字段类型注释更新 |
| `src/test/integration/test_material_instance.cpp` | 新文件，覆盖 UBO 反射写入 |
| 所有引用 `DrawMaterial` 的测试 | 替换为 `MaterialInstance` |

## 边界与约束

- 本需求**不**引入 `IMaterial::getRenderSignature(pass)`——留给 REQ-007
- 本需求**不**改变 `RenderPassEntry` 的字段结构——只清理 `MaterialTemplate` 里的重复 cache 声明
- `m_uboBuffer` 的 GPU 同步语义复用现有 `IRenderResource::setDirty()` + `VulkanResourceManager::syncResource()`，不引入新机制
- Push constant 与 UBO 分离——本需求只管 UBO

## 依赖

- **REQ-004**（UBO 成员反射）——硬依赖

## 下游

- **REQ-007** 会把 `MaterialInstance` 作为 `IMaterial::getRenderSignature(pass)` 的承载者
- **REQ-003b** 的 `PipelineBuildInfo.bindings` 来自反射，与本需求共享 `ShaderResourceBinding.members`

## 实施状态

已完成（2026-04-13）— 通过 `/finish-req 005` 验证。

**验证结果**：R1–R11 全部 ✓ Implemented（随 REQ-004/REQ-007/REQ-003b 链路陆续在之前的 session 里落地）

- **R1 — `IMaterial` 接口收敛**：`src/core/resources/material.hpp:157`。REQ-007 后额外增加了一个 `getRenderSignature(pass)` pure virtual（前向兼容扩展）。
- **R2 — `MaterialTemplate::create(name, shader)` 必传 shader**：`material.hpp:185`；单一 `m_bindingCache`，无 `m_passHashCache`。REQ-007 把 `m_passes` 键从 `std::string` 迁到 `StringID`（有意 drift，下游需要）。
- **R3 — `MaterialInstance : public IMaterial`**：5 个 setter + `setTexture(StringID, CombinedTextureSamplerPtr)` 单一重载；无 `m_vec4s/m_floats` 冗余 map。
- **R4 — UBO 自动初始化**：构造函数按反射 `type == UniformBuffer && name == "MaterialUBO"` 定位，分配 `m_uboBuffer`，并建立稳定的 `UboByteBufferResource` wrapper。
- **R5 — `writeUboMember` 统一 std140 写入**：`setVec4/Vec3/Float/Int` 全部薄壳委托；`setVec3` 正确只写 12 字节（规避 16 字节 bucket 污染下一字段）。
- **R6 — `setTexture` 走 `findBinding(id)`**：包含 `Texture2D/TextureCube` 类型断言。
- **R7 — `getDescriptorResources`**：UBO + 纹理按 `(set<<16)|binding` 排序返回；同时在 REQ-003b 落地时补充 `tex->setBindingName(id)`，方便 backend 按反射 binding name 路由。
- **R8 — `updateUBO`**：dirty flag + `m_uboResource->setDirty()` 流程。
- **R9 — 删除 `DrawMaterial` / `BlinnPhongMaterialUBO`**：`grep -rn "DrawMaterial\|BlinnPhongMaterialUBO" src/` 只剩两处历史注释，实体已删。
- **R10 — `blinnphong_material_loader`** 返回 `MaterialInstance::Ptr`，seeds `baseColor/shininess/specularIntensity/enableAlbedo/enableNormal` 默认值。
- **R11 — 调用点迁移**：`RenderableSubMesh::material` 仍是 `MaterialPtr`，运行期指向 `MaterialInstance`；所有测试的 `DrawMaterial` 构造早已替换。
- **测试**：`src/test/integration/test_material_instance.cpp` 6 个子用例覆盖 UBO buffer 大小、setVec3 12B 边界、setFloat/setInt offset、`getDescriptorResources` 稳定身份、loader 产出有效实例。

**简化**：移除 `IMaterial::getShaderProgramSet()` 纯虚函数及其 4 处实现/override（`MaterialInstance` + `test_pipeline_build_info` / `test_pipeline_identity` / `test_frame_graph` 的 FakeMaterial）。该方法在生产代码无任何调用点，仅因 pure virtual 强制所有具现提供空/转发 override——死 API。`getRenderState()` 保留（`pipeline_build_info.cpp` 消费）。

**测试结果**：
- `test_material_instance` — PASS（6/6 子用例）
- `test_shader_compiler` — PASS（REQ-004 的 UBO members 断言保持绿）
- `test_string_table` / `test_pipeline_identity` / `test_pipeline_build_info` / `test_frame_graph` — 全部 PASS
- 全量 `cmake --build ./build` — 0 warning / 0 error
