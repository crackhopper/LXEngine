# Material System

这是一页 level-1 文档。它不追实现细节，重点回答三件事：

1. 材质系统里哪些对象分别负责什么
2. pass / shader / variants / instance 参数各挂在哪一层
3. 一个 `SceneNode` 为什么能或不能被渲染

细节约束看：

- `openspec/specs/material-system/spec.md`
- `docs/requirements/021-renderable-variants-and-push-constant-cleanup.md`
- `docs/requirements/022-material-pass-selection.md`

## 一句话模型

材质系统采用三层分工：

- `MaterialTemplate`：静态蓝图，定义 pass、shader、variants、render state
- `MaterialInstance`：运行时参数，引用一个 template，持有 UBO/texture 等实例数据
- `SceneNode`：场景装配点，组合 `mesh + materialInstance + optional skeleton`

只有这三层职责清楚，pipeline identity、pass 过滤和合法性校验才不会打架。

## 职责边界

### `MaterialTemplate`

负责“材质长什么样”：

- 定义有哪些 pass
- 每个 pass 用哪套 shader / variants / render state
- 保存 shader 反射得到的 binding 元数据

它是 loader 的主要产物之一。

### `MaterialInstance`

负责“这个材质实例现在的参数是什么”：

- 保存对 template 的引用
- 保存运行时 UBO 内容
- 保存贴图绑定
- 未来负责 instance 级 pass enable/disable

它**不**负责定义 variants，也**不**拥有具体 `Skeleton`。

### `SceneNode`

负责“场景里这个对象如何把几类资源装配在一起”：

- `mesh`
- `materialInstance`
- `optional skeleton`
- `object push constant`

它是合法性校验的第一现场。

## pass 模型

pass 有两层语义：

- `MaterialTemplate` 定义“支持哪些 pass”
- `MaterialInstance` 决定“当前实例启用了哪些 pass”

因此：

- template 是能力上限
- instance 是实际使用子集

一个对象能否进入某个 pass，至少要同时满足：

1. template 定义了该 pass
2. instance 启用了该 pass
3. `SceneNode` 在该 pass 下通过了结构性校验

## shader 与 variants

variants 属于 shader/program 形态，因此归属 `MaterialTemplate`，不归属 `MaterialInstance`。

这意味着：

- 同一个 template 对应一组固定 variants
- 不同 variant 组合应由 loader 构造成不同 template
- instance 只承载参数，不承载 shader 形态

当前关于 `USE_SKINNING` / `USE_LIGHTING` 的决策也是沿着这条边界：

- 它们是 shader variants
- 它们进入 template，而不是 push constant 或 instance 自定义状态

## `Skeleton` 为什么不属于 material

`Skeleton` 是 per-object 运行时资源，不是材质蓝图的一部分。

如果把它挂到 material，会立刻出现三个问题：

- 同一个材质难以复用到多个角色
- 静态 mesh 和 skinned mesh 共用材质时职责混乱
- 动画更新入口会错误地下沉到材质层

因此正确分工是：

- material/template 决定“是否需要 Bones 资源”
- `SceneNode` 决定“当前提供哪个 skeleton/Bones”
- 校验层保证二者匹配

## 合法性校验

校验应在 `SceneNode` 创建完成后立即发生，并在结构性成员变化时重新触发。

校验内容至少包括：

- material 在每个已启用 pass 下的 shader 接口
- mesh vertex layout 是否满足 shader 输入
- descriptor 资源是否齐备
- `USE_SKINNING` 与 `Skeleton/Bones` 是否匹配

失败策略应是：

- 视为程序员错误
- 记录 `FATAL`
- 立即终止进程

这里不做“自动降级渲染”。

## 当前代码状态

当前实现里，已经成立的部分有：

- `MaterialTemplate` 已按 pass 保存 `RenderPassEntry`
- `MaterialInstance` 已是唯一 `IMaterial` 实现
- 实例参数写入已经走 shader 反射

仍处于过渡中的部分有：

- `MaterialInstance` 还没有 instance 级 pass enable/disable
- `getRenderState()` 仍然偏 Forward-only
- 旧 `IRenderable` 接口过于贴近底层资源展开
- vertex input 反射与 SceneNode 级合法性校验还未完整建立

## 推荐阅读顺序

1. 先看本页，建立边界
2. 再看 `notes/subsystems/material-system.md`，了解当前实现
3. 最后看 REQ-021 / REQ-022，理解这轮重构为什么要动这些边界
