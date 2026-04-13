# Pipeline Identity

> Pipeline 的身份不是一个 hash —— 它是一棵**结构化 StringID 树**。REQ-007 之后，所有参与 pipeline 唯一性的资源都通过 `getRenderSignature(pass) → StringID` 贡献自己的那一层，最后由 `PipelineKey::build(objSig, materialSig)` 做两级 compose 得到最终身份。可通过 `toDebugString()` 完整还原成人类可读的 pipeline tree。
>
> 权威 spec: `openspec/specs/pipeline-key/spec.md` + `openspec/specs/render-signature/spec.md` + `openspec/specs/pipeline-build-info/spec.md`

## 核心抽象

### `PipelineKey` (`src/core/resources/pipeline_key.hpp:11`)

```cpp
struct PipelineKey {
    StringID id;

    bool operator==(const PipelineKey&) const;
    bool operator!=(const PipelineKey&) const;

    struct Hash { size_t operator()(const PipelineKey&) const; };

    static PipelineKey build(StringID objectSig, StringID materialSig);
};
```

内部就一个 `StringID`。`build()` 做 `compose(TypeTag::PipelineKey, {objectSig, materialSig})`。

### `PipelineBuildInfo` (`src/core/resources/pipeline_build_info.hpp:31`)

Backend 构建 Vulkan pipeline 所需的**全部**数据包。Backend-agnostic（不引用 `VkXxx`）。

```cpp
struct PipelineBuildInfo {
    PipelineKey                          key;
    std::vector<ShaderStageCode>         stages;       // SPIR-V bytecode
    std::vector<ShaderResourceBinding>   bindings;     // 反射 binding
    VertexLayout                         vertexLayout;
    RenderState                          renderState;
    PrimitiveTopology                    topology;
    PushConstantRange                    pushConstant;

    static PipelineBuildInfo fromRenderingItem(const RenderingItem &item);
};
```

`PushConstantRange` (`:18`) 是 `{stageFlags, offset, size}` 的 core 层值类型。目前由工厂注入引擎约定（128 字节 vertex+fragment），未来可由 shader 声明。

### Per-Resource `getRenderSignature` 贡献方

每个参与 pipeline 身份的资源都提供 `StringID getRenderSignature() const`（或 `(pass)` 版本）：

| 资源 | 签名函数 | 输出 |
|------|---------|------|
| `VertexLayoutItem` | `getRenderSignature()` | 叶子 Intern: `"0_pos_Float3_Vertex_0"` 等 |
| `VertexLayout` | `getRenderSignature()` | `compose(VertexLayout, {items..., Intern(stride)})` |
| `PrimitiveTopology` | `topologySignature(t)` 自由函数 | 叶子 `Intern("tri")` / `Intern("line")` / ... |
| `Mesh` | `getRenderSignature(pass)` | `compose(MeshRender, {layoutSig, topologySig})` |
| `Skeleton` | `getRenderSignature()` | `Intern("Skn1")`（无骨骼时调用方返回 `StringID{}`） |
| `RenderState` | `getRenderSignature()` | `compose(RenderState, {cullTag, depthTags, blendTags, ...})` |
| `ShaderProgramSet` | `getRenderSignature()` | `compose(ShaderProgram, {Intern(name), Intern(variant1), Intern(variant2), ...})`（variant sorted） |
| `RenderPassEntry` | `getRenderSignature()` | `compose(RenderPassEntry, {shaderSig, stateSig})` |
| `MaterialInstance` | `getRenderSignature(pass)` override `IMaterial` | `compose(MaterialRender, {template->getRenderPassSignature(pass)})` |
| `RenderableSubMesh` | `getRenderSignature(pass)` override `IRenderable` | `compose(ObjectRender, {meshSig, skelSig})` |

## 典型用法

### 构造 PipelineKey

```cpp
// 不要自己调 compose —— Scene 会替你做
auto item = scene->buildRenderingItem(Pass_Forward);
// item.pipelineKey 已填充

// 调试还原
auto &tbl = GlobalStringTable::get();
std::cout << tbl.toDebugString(item.pipelineKey.id) << '\n';
// → "PipelineKey(
//      ObjectRender(
//        MeshRender(VertexLayout(0_pos_..., 1_norm_..., 24), tri),
//        Skn1
//      ),
//      MaterialRender(
//        RenderPassEntry(
//          ShaderProgram(blinnphong_0, HAS_NORMAL_MAP),
//          RenderState(CullBack, DepthTest, DepthWrite, LessEqual, NoBlend, One, Zero)
//        )
//      )
//    )"
```

### 从 RenderingItem 推导 PipelineBuildInfo

```cpp
auto info = PipelineBuildInfo::fromRenderingItem(item);
// info.key == item.pipelineKey
// info.bindings == item.shaderInfo->getReflectionBindings()（同序）
// info.stages   == item.shaderInfo->getAllStages()
// info.vertexLayout / topology / renderState 从 item 各字段派生
```

Backend 拿到 `PipelineBuildInfo` 就能构建完整的 `VulkanPipeline`，**不需要任何 shader 名字查表**，也不需要硬编码的 descriptor slot 枚举。

## 调用关系

```
Scene::buildRenderingItem(Pass_Forward)
  │
  ├── sub->getRenderSignature(Pass_Forward)              // objectSig
  │     ├── mesh->getRenderSignature(pass)
  │     │     └── compose(MeshRender, {layoutSig, topoSig})
  │     └── skeleton.value()->getRenderSignature()
  │           └── Intern("Skn1")
  │
  ├── sub->material->getRenderSignature(Pass_Forward)    // materialSig
  │     └── template->getRenderPassSignature(Pass_Forward)
  │           └── entry.getRenderSignature()
  │                 └── compose(RenderPassEntry, {shaderSig, stateSig})
  │
  └── PipelineKey::build(objectSig, materialSig)
        └── compose(TypeTag::PipelineKey, {objectSig, materialSig})
              ↓
        item.pipelineKey = { .id = ... }
              ↓
    PipelineBuildInfo::fromRenderingItem(item)
              ↓
    PipelineCache::preload / getOrCreate(info, renderPass)
              ↓
        VulkanPipeline
```

## 注意事项

- **两级 compose 是故意的**: object + material 两个子身份分开 compose，好处是 debug string 分层清晰，并且未来如果要按 pass 做 pipeline variant（例如 `Pass_Shadow` 的 stripped state），只要在 `MaterialInstance::getRenderSignature(pass)` 里换一个 `RenderPassEntry` 就行，不用改 `PipelineKey::build` 的签名。
- **Pass 参数统一传**: `getRenderSignature(pass)` 在所有 `IRenderable` / `Mesh` 上都必须接 pass 参数，即便当前实现忽略它（`Mesh` 就是这样，注释里写明了）。签名统一的原因是未来要"同一个 mesh 在不同 pass 剔除属性"时可以扩展而不改接口。
- **Skeleton 用 `Intern("Skn1")` 而非 compose**: 因为骨骼对 pipeline 的贡献只是一个布尔（"启用骨骼 / 不启用"），叶子字符串就够了。无骨骼的情况由调用方返回 `StringID{}`（id = 0），**不**要让 `Skeleton::getRenderSignature()` 自己返回 0，这样才能保持类本身的语义干净。
- **Variant 必须 sort**: `ShaderProgramSet::getRenderSignature()` 在 compose 前对 `variants.macroName` 做字典序排序。否则 `{A,B}` 和 `{B,A}` 会算出不同的 key。
- **`getPipelineHash() → size_t` 已废弃**: REQ-007 之前这条通道用 `hash_combine` 把各组件的 hash 混成一个 `size_t`。现在全部走 `getRenderSignature()`，hash-based 版本只是历史残留（已在代码里被删除）。如果看到任何地方还有 `getPipelineHash`，那就是 bug。

## 测试

- `src/test/integration/test_pipeline_identity.cpp` — 覆盖两级 compose + 各 resource 的签名独立性 + `toDebugString` 可读性
- `src/test/integration/test_pipeline_build_info.cpp` — `fromRenderingItem` 的字段派生 + 跨调用确定性

## 延伸阅读

- `openspec/specs/pipeline-key/spec.md` — `PipelineKey::build(objSig, matSig)` + `toDebugString` 要求
- `openspec/specs/render-signature/spec.md` — 每类资源的 `getRenderSignature` 签名与语义
- `openspec/specs/pipeline-build-info/spec.md` — `fromRenderingItem` 工厂与 `bindings` 保序要求
- 归档: `openspec/changes/archive/2026-04-10-pipeline-key-rendering-item/` — REQ-002 的原始 hash-based 版本
- 归档: `openspec/changes/archive/2026-04-13-interning-pipeline-identity/` — REQ-007 迁移到 interning
- 归档: `openspec/changes/archive/2026-04-13-pipeline-prebuilding/` — REQ-003b 加入 `PipelineBuildInfo`
