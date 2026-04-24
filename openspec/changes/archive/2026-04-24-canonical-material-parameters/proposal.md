## Why

当前材质系统允许 `MaterialInstance` 在“实例级默认值”之外，再为某个 pass 保存 `PassMaterialOverride` 副本。这让同一个材质实例同时存在多份参数真值，增加了状态模型、同步路径和后续资源复用的复杂度，也不利于未来向 bindless / descriptor 复用方向收敛。

现在需要把材质参数模型收敛成“单一 canonical 参数集 + pass 使用声明 + 反射校验”的形式，让 `MaterialInstance` 成为唯一的数据来源，并把不同 shader/pass 之间的兼容性问题前置到模板构建和反射校验阶段解决。

## What Changes

- **BREAKING** 移除 `MaterialInstance` 的 per-pass 参数覆盖模型；`PassMaterialOverride` 不再保存 buffer / texture 副本。
- `MaterialInstance` 只保存一份 canonical 材质参数与资源，作为所有 pass 的唯一运行时真值。
- `MaterialTemplate` 改为显式定义每个 pass 使用哪些 canonical parameter/resource binding，并在构建期校验跨 pass 复用是否合法。
- `MaterialInstance::getDescriptorResources(pass)` 改为按 pass 的“已声明使用集合”从 canonical 参数集中选取资源，而不是从 pass override 回退。
- 明确 shader reflection 的职责：校验不同 shader 中同名 material-owned binding 的 resource type、layout、member 定义是否兼容，并建立 pass 到 canonical parameter slot 的映射。
- 更新文档、源码分析和注释，说明 canonical parameter model、pass usage contract，以及它与 bindless / shader 复用的关系。

## Capabilities

### New Capabilities

### Modified Capabilities
- `material-system`: MaterialTemplate / MaterialInstance contract changes from per-pass override data to canonical shared parameters with pass-scoped usage declarations and reflection-backed compatibility validation.

## Impact

- Affected code: `src/core/asset/material_template.*`, `src/core/asset/material_instance.*`, material loaders, scene/material validation paths, and backend descriptor resource assembly.
- Affected docs/specs: material-system spec, subsystem notes, source analysis pages, and code comments around `IGpuResource` / material parameter ownership.
- Affected APIs: per-pass parameter/texture override behavior is removed; pass-aware reads become usage-filtered views over canonical instance data.
