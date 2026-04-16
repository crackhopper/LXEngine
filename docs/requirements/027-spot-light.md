# REQ-027: SpotLight

## 背景

当前光源系统只有 `DirectionalLight`。这足够支撑最小 forward scene，但不足以支撑“聚光灯照亮局部区域”这种非常基础的完整引擎场景。

概念层已经需要讲“光源系统”，因此 `SpotLight` 需要从 roadmap 项收敛成可单独引用的 requirement。

## 目标

1. 引入 `SpotLight : LightBase`。
2. 提供最小可用的 `SpotLightUBO`。
3. 让 scene 可以像管理方向光一样管理聚光灯对象。

## 需求

### R1: 新增 `SpotLight` 运行时对象

- `SpotLight` 继承 `LightBase`。
- 至少包含 `position`、`direction`、`color/intensity`、`innerCone`、`outerCone`。
- 保留现有 `getPassMask()` / `getUBO()` / `supportsPass(pass)` 入口语义。

### R2: `SpotLightUBO` 必须是 scene-level 资源

- 聚光灯资源和方向光一样，作为 scene-level descriptor resource 进入 shader。
- 首版不要求支持阴影贴图，只要求 forward 光照消费路径可用。

### R3: scene 管理方式与 `DirectionalLight` 对齐

- `Scene::addLight(...)` 不应为 `SpotLight` 单独加特殊接口。
- `Scene::getSceneLevelResources(pass, target)` 需要能把命中 pass 的 spot light 资源一并追加。

### R4: 概念层明确“SpotLight 不等于多光源已完成"

- 本 REQ 只解决 `SpotLight` 作为单个 light 类型的引入。
- 多光源聚合、固定上限、shader 循环合同由 [`REQ-029`](029-multi-light-scene-resource-model.md) 负责。

## 修改范围

- `src/core/scene/light.*`
- `src/core/scene/scene.*`
- `notes/concepts/light/`
- 相关 shader / demo

## 依赖

- 现有 `LightBase` / `DirectionalLight` 架构
- 后续多光源资源模型由 [`REQ-029`](029-multi-light-scene-resource-model.md) 承接

## 实施状态

未开始。
