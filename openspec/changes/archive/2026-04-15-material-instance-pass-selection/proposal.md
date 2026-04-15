## Why

当前 pass 的“定义”已经在 `MaterialTemplate` 上，但 pass 的“实例启停”仍混在 `MaterialInstance::m_passFlag` 这个粗粒度 bitmask 里，导致 template、instance、`SceneNode` 和 `supportsPass(pass)` 的职责链仍不对齐。随着多 pass 渲染推进，这会继续放大 Forward-only 过渡接口、共享 `MaterialInstance` 行为不透明、以及结构性重校验传播边界不清的问题。

## What Changes

- 明确 `MaterialTemplate` 是 pass 蓝图所有者：定义 pass 集合、对应 `RenderPassEntry`、shader、variants 和 render state。
- 为 `MaterialInstance` 增加 instance 级 pass enable/disable 语义，并规定默认启用 template 中定义的全部 pass。
- 要求 `setPassEnabled(pass, ...)` 对 template 未定义的 pass 统一 `FATAL + terminate`。
- 要求 `getPassFlag()` 从“template 已定义且 instance 已启用”的 pass 集合推导，而不是再维护独立真值 bitmask。
- **BREAKING**：`getRenderState()` 不再允许保留 Forward-only 语义，材质 render state 查询必须 pass-aware。
- 要求 `SceneNode` / `supportsPass(pass)` / 结构性校验接入新的实例级 pass 状态，并把 pass enable 变化视为结构性变化。
- 要求 `Scene` 负责将共享 `MaterialInstance` 的 pass 状态变化传播到所有引用它的节点，例如通过 `revalidateNodesUsing(materialInstance)` 一类接口。
- 明确普通材质参数变化（`setFloat` / `setTexture` / `updateUBO` 等）不是结构性变化，不得触发 pass 级重新校验。
- 明确本期 `MaterialTemplate` 视为静态蓝图，不支持运行时结构性热修改。

## Capabilities

### New Capabilities

### Modified Capabilities
- `material-system`: 增加 instance 级 pass enable/disable、默认启用全部 template passes、pass-aware `getRenderState()`、以及由已定义且已启用 pass 集推导 `getPassFlag()` 的要求。
- `scene-node-validation`: 增加对实例级 pass 状态变化的结构性重校验语义、共享 `MaterialInstance` 影响所有引用节点的传播要求、以及场景层 `revalidateNodesUsing(materialInstance)` 职责边界。

## Impact

- 主要影响 [material.hpp](/home/lx/proj/renderer-demo/src/core/asset/material.hpp:1)、[material.cpp](/home/lx/proj/renderer-demo/src/core/asset/material.cpp:1)、`src/core/scene/` 下的 `Scene` / `SceneNode` 路径，以及依赖 `getPassFlag()` / `supportsPass(pass)` 的 queue 过滤逻辑。
- 需要调整材质实例的公共 API、pass 查询调用点、结构性缓存失效/重建路径，以及共享实例的测试夹具。
- 需要同步更新 `docs/design/MaterialSystem.md` 与 `notes/subsystems/material-system.md` / `scene.md` 等实现文档。
