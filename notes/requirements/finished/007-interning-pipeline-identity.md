# REQ-007: 结构化 Interning 驱动的 Pipeline Identity

> **Supersedes**：本需求废弃并替换 REQ-001 R6/R7、REQ-002 R3/R4 中由 hash 通道驱动的 pipeline identity 约定。实施本需求时需同步把 `finished/001-...md` 和 `finished/002-...md` 标注为 "Superseded by REQ-007"（见 REQ-007 R11）。
>
> **Partial supersede by REQ-008**：本文档 R9 `Scene::buildRenderingItem(StringID pass)` 作为对外入口的约定已被 REQ-008 废弃。`RenderingItem` 的构造职责转移到 `RenderQueue::buildFromScene(scene, pass)` —— `PipelineKey::build(objSig, matSig)` 的计算路径不变，只是 host 从 `Scene` 迁到 `RenderQueue`。R1–R8 / R10 继续有效（`getRenderSignature(pass)` 的结构化 interning 规则没有变化）。归档保留历史上下文；当前实现以 REQ-008 为准。

## 背景

REQ-001/REQ-002 落地后，pipeline identity 由各资源的 `getPipelineHash() → size_t` 贡献，`PipelineKey::build(...)` 把 shader、mesh、render state、skeleton 的 hash 组合成一个字符串后喂给 `StringID`。这套方案解决了"有一个稳定 key 可用"的问题，但存在三个缺点：

1. **可读性差**：最终 key 字符串形如 `blinnphong_0||ml:0x3a2f1b7c|rs:0x7c1de4a0|sk:0x0`，调试时只能看到几个 16 进制 hash，不知道是什么 layout、什么 variants
2. **脆弱**：`Mesh` 的 `getPipelineHash()` 把 layout + topology 混在一起 `hash_combine`，任何子成员的 hash 改变都会重建整个 pipeline，无法做局部命中分析
3. **pass 参数缺失**：当前 identity 不含 pass 概念。引入多 pass（Forward / Shadow / Deferred）后，同一个 mesh+material 在不同 pass 下需要不同 pipeline，现有结构无法表达

REQ-006 已经为 `GlobalStringTable` 加上了结构化 compose 能力。本需求用它把 pipeline identity 改造成一棵**结构化 StringID 树**，叶子是人类可读的名字，中间节点带 `TypeTag`，最终 `PipelineKey.id` 可通过 `toDebugString()` 完整还原成 `PipelineKey(ObjectRender(...), MaterialRender(RenderPassEntry(ShaderProgram(blinnphong_0), RenderState(CullBack, DepthTest, ...))))` 这种形态。

## 目标

1. 所有参与 pipeline identity 的资源类提供 **`getRenderSignature(pass) → StringID`**（叶子资源可省略 `pass` 参数）
2. `PipelineKey::build(objectSig, materialSig)` 两级 compose，不再接收 mesh/state/skeleton 的散包
3. 引入 **pass 参数**贯通 `Scene::buildRenderingItem(StringID pass)`、`RenderingItem.pass`、`IRenderable::getRenderSignature(pass)`、`IMaterial::getRenderSignature(pass)`
4. `Pass_Forward / Pass_Deferred / Pass_Shadow` 作为预设 StringID 常量
5. 废除 `getPipelineHash()`、`getLayoutHash()`、`getHash()` 的 pipeline-identity 职责（它们仍可用作内部 hash map 键，但不再参与 PipelineKey 构造）

## 需求

### R1: Pass 系统常量

```cpp
// src/core/scene/pass.hpp（新文件）
namespace LX_core {

inline const StringID Pass_Forward  = StringID("Forward");
inline const StringID Pass_Deferred = StringID("Deferred");
inline const StringID Pass_Shadow   = StringID("Shadow");

}
```

> 用 `inline const` 而非 `inline constexpr`：`StringID` 构造有 side effect（intern 到全局表），不能 constexpr。

### R2: getRenderSignature 的签名约定

| 资源 | 签名 | 含 pass 参数? |
|------|------|---------------|
| `VertexLayoutItem` | `StringID getRenderSignature() const` | 否 |
| `VertexLayout` | `StringID getRenderSignature() const` | 否 |
| `RenderState` | `StringID getRenderSignature() const` | 否 |
| `ShaderProgramSet` | `StringID getRenderSignature() const` | 否 |
| `PrimitiveTopology` | 自由函数 `StringID topologySignature(PrimitiveTopology)` | 否 |
| `Skeleton` | `StringID getRenderSignature() const` | 否（Skeleton 只影响 vertex 路径，与 pass 无关） |
| `Mesh` | `StringID getRenderSignature(StringID pass) const` | **是**（当前 pass 参数暂不改变结果，但签名必须统一） |
| `RenderPassEntry` | `StringID getRenderSignature() const` | 否（RenderPassEntry 本身就是 per-pass 的） |
| `MaterialTemplate` | `StringID getRenderPassSignature(StringID pass) const` | 是 |
| `IMaterial` | `StringID getRenderSignature(StringID pass) const` | 是 |
| `IRenderable` | `StringID getRenderSignature(StringID pass) const` | 是 |

**简化决定**：`Mesh::getRenderSignature(pass)` 的 `pass` 参数当前会被忽略，但保留签名一致性，避免调用方做特例。未来若出现"同一个 mesh 在不同 pass 剔除属性"，只需在实现里消费参数。

### R3: VertexLayoutItem 与 VertexLayout

```cpp
// vertex_buffer.hpp
StringID VertexLayoutItem::getRenderSignature() const {
  // "{location}_{name}_{type}_{inputRate}_{offset}"
  std::string tag;
  tag.reserve(name.size() + 32);
  tag += std::to_string(location);
  tag += '_';
  tag += name;
  tag += '_';
  tag += toString(type);       // Float3 / Float4 / ...
  tag += '_';
  tag += toString(inputRate);  // Vertex / Instance
  tag += '_';
  tag += std::to_string(offset);
  return GlobalStringTable::get().Intern(tag);
}

StringID VertexLayout::getRenderSignature() const {
  std::vector<StringID> parts;
  parts.reserve(m_items.size() + 1);
  for (const auto &item : m_items)
    parts.push_back(item.getRenderSignature());
  parts.push_back(GlobalStringTable::get().Intern(std::to_string(m_stride)));
  return GlobalStringTable::get().compose(TypeTag::VertexLayout, parts);
}
```

> **决定**：VertexLayoutItem 包含 `offset` 但不包含 `size`（`size` 由 `type` 完全决定）。两个 layout 同样 attribute、不同 interleaving 会产生不同 vertex shader 读取路径，pipeline 必须不同。

### R4: PrimitiveTopology

```cpp
// index_buffer.hpp
StringID topologySignature(PrimitiveTopology t);   // 自由函数，不污染 enum
```

实现把原 `pipeline_key.cpp` 里的 `topologyTag()` 的 switch 搬到 `index_buffer.cpp`，每个 case 返回 `Intern("point" / "line" / "tri" / ...)`。原 `topologyTag()` 连带 `pipeline_key.cpp` 里的局部辅助一并删除。

### R5: Mesh / Skeleton / ShaderProgramSet / RenderState

```cpp
// mesh.hpp
StringID Mesh::getRenderSignature(StringID /*pass*/) const {
  return GlobalStringTable::get().compose(
    TypeTag::MeshRender, {
      vertexBuffer->getLayout().getRenderSignature(),
      topologySignature(indexBuffer->getTopology()),
    });
}
```

```cpp
// skeleton.hpp
StringID Skeleton::getRenderSignature() const {
  // Skeleton 实例存在即代表启用骨骼；无骨骼场景由调用方返回 StringID{0}
  return GlobalStringTable::get().Intern("Skn1");
}
```

```cpp
// shader.hpp
StringID ShaderProgramSet::getRenderSignature() const {
  std::vector<StringID> parts;
  parts.reserve(1 + variants.size());
  parts.push_back(GlobalStringTable::get().Intern(shaderName));

  std::vector<std::string_view> enabled;
  for (const auto &v : variants)
    if (v.enabled) enabled.push_back(v.macroName);
  std::sort(enabled.begin(), enabled.end());   // 必须排序

  for (auto m : enabled)
    parts.push_back(GlobalStringTable::get().Intern(std::string(m)));

  return GlobalStringTable::get().compose(TypeTag::ShaderProgram, parts);
}
```

```cpp
// material.hpp
StringID RenderState::getRenderSignature() const {
  auto &tbl = GlobalStringTable::get();
  return tbl.compose(TypeTag::RenderState, {
    tbl.Intern(toString(cullMode)),
    tbl.Intern(depthTestEnable ? "DepthTest" : "NoDepthTest"),
    tbl.Intern(depthWriteEnable ? "DepthWrite" : "NoDepthWrite"),
    tbl.Intern(toString(depthOp)),
    tbl.Intern(blendEnable ? "Blend" : "NoBlend"),
    tbl.Intern(toString(srcBlend)),
    tbl.Intern(toString(dstBlend)),
  });
}
```

`toString(CullMode/CompareOp/BlendFactor)` 如果不存在则一并新增（纯静态函数，写在 material.hpp 或 material.cpp）。

### R6: RenderPassEntry / MaterialTemplate / MaterialInstance

```cpp
// material.hpp
StringID RenderPassEntry::getRenderSignature() const {
  return GlobalStringTable::get().compose(
    TypeTag::RenderPassEntry, {
      shaderSet.getRenderSignature(),
      renderState.getRenderSignature(),
    });
}

StringID MaterialTemplate::getRenderPassSignature(StringID pass) const {
  // 内部存储 key 从 std::string 迁移到 StringID（见 R9）
  auto it = m_passes.find(pass);
  if (it == m_passes.end()) return StringID{};
  return it->second.getRenderSignature();
}

// MaterialInstance（REQ-005 已让它实现 IMaterial）
StringID MaterialInstance::getRenderSignature(StringID pass) const override {
  StringID passSig = m_template->getRenderPassSignature(pass);
  return GlobalStringTable::get().compose(TypeTag::MaterialRender, {passSig});
}
```

### R7: IRenderable / RenderableSubMesh

```cpp
// object.hpp
class IRenderable {
public:
  virtual StringID getRenderSignature(StringID pass) const = 0;
  // ... 其他接口不变 ...
};

// RenderableSubMesh
StringID RenderableSubMesh::getRenderSignature(StringID pass) const override {
  StringID meshSig = mesh->getRenderSignature(pass);
  StringID skelSig = skeleton.has_value()
                       ? skeleton.value()->getRenderSignature()
                       : StringID{};
  return GlobalStringTable::get().compose(
    TypeTag::ObjectRender, {meshSig, skelSig});
}
```

> 草案 §3.2 的 `skeleton->getRenderSignature()` 遗漏了 `optional::value()` 解包，本文档已修正。

### R8: PipelineKey::build 两级 compose

```cpp
// pipeline_key.hpp
struct PipelineKey {
  StringID id;

  static PipelineKey build(StringID objectSig, StringID materialSig);
  // ... operator== / Hash 不变 ...
};
```

```cpp
// pipeline_key.cpp
PipelineKey PipelineKey::build(StringID objectSig, StringID materialSig) {
  return PipelineKey{
    GlobalStringTable::get().compose(
      TypeTag::PipelineKey, {objectSig, materialSig})
  };
}
```

**删除项**：

- 原 `PipelineKey::build(ShaderProgramSet, Mesh, RenderState, SkeletonPtr)` 签名（REQ-001 R7 引入的形态）
- `pipeline_key.cpp` 中的 `variantSegment()` / `topologyTag()` 辅助函数
- `pipeline_key.hpp` 对 `Mesh / RenderState / Skeleton` 的 include（只依赖 `StringID`）

### R9: Scene::buildRenderingItem(pass) 与 RenderingItem.pass

```cpp
// scene.hpp
struct RenderingItem {
  // ... 现有字段 ...
  StringID    pass;           // 新增
  PipelineKey pipelineKey;    // 已存在
};

class Scene {
public:
  RenderingItem buildRenderingItem(StringID pass);   // 新增 pass 参数
};
```

```cpp
// scene.cpp
RenderingItem Scene::buildRenderingItem(StringID pass) {
  RenderingItem item;
  // ... 原有 vertexBuffer/indexBuffer/... 收集 ...
  item.pass = pass;

  auto sub = std::dynamic_pointer_cast<RenderableSubMesh>(mesh);
  if (sub && sub->mesh && sub->material) {
    StringID objSig = sub->getRenderSignature(pass);
    StringID matSig = sub->material->getRenderSignature(pass);
    item.pipelineKey = PipelineKey::build(objSig, matSig);
  }
  return item;
}
```

**注意**：`MaterialTemplate` 当前按 `std::string` 存 pass（见 `material.hpp:107`）。本需求将其改为 `unordered_map<StringID, RenderPassEntry>`，`setPass(StringID, RenderPassEntry)` 重载取代现有 `setPass(const std::string&, ...)`。调用点（`blinnphong_material_loader` 等）统一传 `Pass_Forward` 等 StringID 常量。

### R10: 显式废弃清单

实施本需求时，**必须移除**：

| 文件 | 删除项 |
|------|--------|
| `src/core/resources/mesh.hpp` | `Mesh::getPipelineHash()`（`getLayoutHash()` 保留，但它不再参与 PipelineKey） |
| `src/core/resources/skeleton.hpp` | `Skeleton::getPipelineHash()` 和 `kSkeletonPipelineHashTag`；简化为只暴露 `getRenderSignature()` |
| `src/core/resources/material.hpp` | `RenderState::getPipelineHash()`、`RenderPassEntry::getPipelineHash()`、`MaterialTemplate::getPipelineHash(pass)`、`m_passHashCache` |
| `src/core/resources/shader.hpp` | `ShaderProgramSet::getPipelineHash()`（保留 `getHash()` 作为内部 hash map 键） |
| `src/core/resources/pipeline_key.cpp` | 旧 `build(ShaderProgramSet, Mesh, ...)` 实现 + 辅助函数 |

### R11: 归档文档标注

修改 `docs/requirements/finished/001-skeleton-to-resources.md` 和 `docs/requirements/finished/002-pipeline-key.md` 的顶部，添加 banner：

```markdown
> **Superseded by REQ-007**：本文档记录的 R6/R7（001）与 R3/R4（002）关于 `getPipelineHash()` 与 `PipelineKey::build()` 签名的约定已由 REQ-007「结构化 Interning 驱动的 Pipeline Identity」替换。归档保留历史上下文；当前实现以 REQ-007 为准。
```

> 例外：这是对 finished/ 目录的唯一允许修改——仅加废弃标注，**不改正文**。

## 数据流

```
Scene::buildRenderingItem(Pass_Forward)
  │
  ├── sub->getRenderSignature(Pass_Forward)
  │     │
  │     ├── mesh->getRenderSignature(pass)
  │     │     └── compose(MeshRender, [
  │     │            compose(VertexLayout, [Intern("0_pos_Float3_Vertex_0"), ...]),
  │     │            Intern("tri")
  │     │          ])
  │     └── skeleton.value()->getRenderSignature()
  │           └── Intern("Skn1")
  │   => compose(ObjectRender, [meshSig, skelSig])
  │
  ├── material->getRenderSignature(Pass_Forward)
  │     └── template->getRenderPassSignature(Pass_Forward)
  │           └── entry.getRenderSignature()
  │                 └── compose(RenderPassEntry, [
  │                        compose(ShaderProgram, [Intern("blinnphong_0"), Intern("HAS_NORMAL_MAP")]),
  │                        compose(RenderState, [Intern("CullBack"), Intern("DepthTest"), ...]),
  │                      ])
  │   => compose(MaterialRender, [passSig])
  │
  └── PipelineKey::build(objSig, matSig)
        => compose(PipelineKey, [objSig, matSig])

最终 PipelineKey.id 通过 GlobalStringTable::toDebugString() 展开：

PipelineKey(
  ObjectRender(
    MeshRender(VertexLayout(0_pos_Float3_Vertex_0, 1_norm_Float3_Vertex_12, 24), tri),
    Skn1
  ),
  MaterialRender(
    RenderPassEntry(
      ShaderProgram(blinnphong_0, HAS_NORMAL_MAP),
      RenderState(CullBack, DepthTest, DepthWrite, LessEqual, NoBlend, One, Zero)
    )
  )
)
```

## 测试

1. **`test_string_table.cpp`（REQ-006）扩展**：compose PipelineKey 的 toDebugString 输出
2. **新增 `test_pipeline_identity.cpp`**：
   - 两个 RenderableSubMesh 相同 layout / topology / shader / state → 同一 PipelineKey
   - 改 variant 后 → 不同 PipelineKey
   - 改 topology 后 → 不同 PipelineKey
   - 加 skeleton → 不同 PipelineKey
   - 改 pass（Forward ↔ Shadow，同一 template 配置不同 entry）→ 不同 PipelineKey
3. **Backend 回归**：`test_render_triangle.cpp` 依然跑通，pipeline cache 命中率不降低
4. **构建**：全绿

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/scene/pass.hpp` | **新** — Pass 常量 |
| `src/core/utils/string_table.hpp` | （由 REQ-006 扩展） |
| `src/core/resources/vertex_buffer.hpp` | `VertexLayoutItem/VertexLayout::getRenderSignature` |
| `src/core/resources/index_buffer.{hpp,cpp}` | `topologySignature(PrimitiveTopology)` |
| `src/core/resources/mesh.hpp` | `getRenderSignature(pass)`；删 `getPipelineHash` |
| `src/core/resources/skeleton.hpp` | `getRenderSignature()`；删 `getPipelineHash` / `kSkeletonPipelineHashTag` |
| `src/core/resources/shader.hpp` | `ShaderProgramSet::getRenderSignature`；删 `getPipelineHash` |
| `src/core/resources/material.hpp` | `RenderState::getRenderSignature`；`RenderPassEntry::getRenderSignature`；`MaterialTemplate` key 改 `StringID`；`MaterialInstance::getRenderSignature(pass)`（override）；删旧 `getPipelineHash` |
| `src/core/resources/pipeline_key.{hpp,cpp}` | 两级 compose；删旧 build |
| `src/core/scene/object.hpp` | `IRenderable::getRenderSignature(pass)`；`RenderableSubMesh::getRenderSignature(pass)` override |
| `src/core/scene/scene.{hpp,cpp}` | `buildRenderingItem(StringID pass)`；`RenderingItem.pass` 字段 |
| `src/backend/vulkan/details/vk_resource_manager.cpp` | `getOrCreateRenderPipeline` 仍按 `item.pipelineKey` 查找；无需改动 |
| `src/test/integration/test_pipeline_identity.cpp` | 新 |
| `docs/requirements/finished/001-skeleton-to-resources.md` | 顶部加 Superseded banner |
| `docs/requirements/finished/002-pipeline-key.md` | 顶部加 Superseded banner |

## 边界与约束

- `getHash()` / `getLayoutHash()` 等"内部 hash 辅助"**保留**，它们仍可被 `unordered_map` 使用。废除的是"作为 pipeline identity 贡献者"的角色
- 本需求**不**改 backend `VulkanPipeline` 的构建逻辑（仍按 `shaderName` 取 slot 表）——那是 REQ-003b 的范围
- 本需求**不**引入 FrameGraph / RenderQueue——`Scene::buildRenderingItem(pass)` 只接受单个 pass 参数，调用方负责在多个 pass 下各调一次
- `MaterialInstance::getRenderSignature(pass)` 依赖 REQ-005 已让 `MaterialInstance` 实现 `IMaterial`

## 依赖

- **REQ-005**（MaterialInstance 是 IMaterial 唯一实现）——硬依赖
- **REQ-006**（GlobalStringTable compose/decompose/TypeTag）——硬依赖

## 下游

- **REQ-003b** 的 `FrameGraph::buildFromScene()` 会为每个 pass 调用 `buildRenderingItem(pass)`，直接消费本需求提供的 pass 参数通道
- 未来 `toDebugString()` 输出可被写入 pipeline 预构建报告 / 日志

## 实施状态

已完成（2026-04-13）— 通过 openspec 变更 `interning-pipeline-identity` 落地。

- 全部 11 个任务组、50+ 子任务完成，`openspec validate --strict` 通过
- `src/core/scene/pass.hpp` — `Pass_Forward / Pass_Deferred / Pass_Shadow` 常量
- 各资源的 `getRenderSignature(...)`：`VertexLayoutItem/Layout`、`topologySignature`、`Mesh`、`Skeleton`、`ShaderProgramSet`、`RenderState`、`RenderPassEntry`、`MaterialTemplate::getRenderPassSignature`、`MaterialInstance`、`RenderableSubMesh`
- `PipelineKey::build(StringID, StringID)` 两级 compose；旧四参重载、`variantSegment()` 辅助、`kSkeletonPipelineHashTag` 全部删除
- `MaterialTemplate::m_passes` 迁移到 `unordered_map<StringID, RenderPassEntry, StringID::Hash>`；`setPass` / `getEntry` 改签名
- `Scene::buildRenderingItem(StringID pass)`，`RenderingItem` 新增 `pass` 字段
- 6 个调用点全量迁移：`blinnphong_material_loader`、`test_material_instance`、`vk_renderer`、`test_vulkan_{pipeline,resource_manager,command_buffer}`
- 新增 `src/test/integration/test_pipeline_identity.cpp`，6 条用例覆盖相同配置/variant/topology/skeleton/pass/toDebugString，全部通过
- 无回归：`test_string_table` / `test_material_instance` / 全量 cmake build 绿
- `finished/001-*.md`、`.../002-*.md` 的 Superseded banner 已存在（前次 session 添加）
