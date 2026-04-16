# REQ-029: 多光源场景资源模型

## 背景

当前 `Scene` 已经能持有多个 `LightBase`，但“多个光源如何稳定进入 shader 并形成一个清晰的 scene-level 资源模型”还没有被正式定义。

如果没有这个 requirement，概念层只能说“Scene 里可以放很多 light 对象”，却不能说清楚：

- shader 究竟如何消费多个 light
- 运行时是否有固定上限
- 不同 light 类型如何共同进入一条 forward lighting 路径

## 目标

1. 定义多光源的 scene-level 资源合同。
2. 让 `DirectionalLight`、`SpotLight` 等类型能共存于同一条 lighting 路径。
3. 给一个“小而完整”的 forward renderer 建立清晰上限和排序规则。

## 需求

### R1: 首版采用固定上限的 forward light set

- 首版定义一个固定上限 `N` 的 light 集合，推荐从 `N=8` 起步。
- 该上限是运行时和 shader 都明确可见的合同，而不是隐式约定。

### R2: scene 需要把 light object 投影成统一 light set

- `Scene` 保持对象级 `LightBase` 容器。
- 在 scene-level 资源装配阶段，把命中当前 pass 的 light objects 投影成统一的 GPU light set。

### R3: 首版必须写死选择规则

当命中 pass 的 light 数量超过上限时，首版必须有确定规则，推荐：

- 方向光优先保留
- 其余局部光按与相机或对象的距离排序后截断

### R4: 统一 light set 不改变 pipeline 身份

- 多光源数量变化属于 scene-level 资源内容变化，不应直接改变 `PipelineKey`。
- 真正影响 pipeline 的仍然是 shader 是否声明并启用了多光源合同。

### R5: 文档必须说明与 `SpotLight`、IBL 的关系

- `SpotLight` 是 light object 类型，引入由 [`REQ-027`](027-spot-light.md) 负责。
- IBL 是环境光资源，接入由 [`REQ-028`](028-ibl-environment-lighting.md) 负责。
- 本 REQ 只定义“多个 light object 作为 scene-level inputs 怎么进入 shader”。

## 修改范围

- `src/core/scene/light.*`
- `src/core/scene/scene.*`
- scene-level shader resource 合同
- `notes/concepts/light/`
- `notes/subsystems/scene.md`

## 依赖

- 现有 `LightBase` / `DirectionalLight`
- [`REQ-027`](027-spot-light.md)

## 实施状态

未开始。
