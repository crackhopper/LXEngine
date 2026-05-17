# 架构总览：三层、两段、一条 draw 主线

LXEngine 的架构可以先想成一座工厂：`core` 画产品蓝图，`infra` 负责把外部材料和工具接进来，`backend` 负责把蓝图真正送上 Vulkan 生产线。场景、材质、shader、pipeline、GPU resource 都沿着这条分工向下流动。

## 三层结构先把责任隔开

| 层 | 类比 | 当前目录 | 主要职责 |
|---|---|---|---|
| `core` | 蓝图室 | `src/core/` | 平台无关的类型、接口、scene、material、frame graph、pipeline identity |
| `infra` | 工具间 | `src/infra/` | shader 编译反射、mesh/texture/material loader、window、ImGui 等工程接入 |
| `backend` | Vulkan 车间 | `src/backend/vulkan/` | device、resource upload、descriptor、pipeline、command buffer、present |

依赖规则很简单：

| 规则 | 意义 |
|---|---|
| `core` 不 include `infra` / `backend` | 核心概念不绑定具体库或 Vulkan 细节 |
| `infra` 依赖 `core` | loader 和工具产出 core 对象 |
| `backend` 消费 `core` / `infra` 结果 | Vulkan 后端把抽象翻译成 GPU 命令 |

## 运行时分成“启动场景”和“执行一帧”

渲染不是每帧从零分析所有东西。当前设计先在场景启动阶段建立结构，再在每帧执行阶段推进数据。

| 阶段 | 发生什么 | 典型入口 |
|---|---|---|
| 启动场景 | 构建 frame graph、render queue、pipeline build desc，预热 pipeline | `EngineLoop::startScene(scene)` |
| 执行一帧 | update、上传 dirty resource、draw | `EngineLoop::tickFrame()` |

启动阶段的主链路：

```text
Scene
  -> FrameGraph::buildFromScene(...)
  -> RenderQueue::buildFromScene(pass, target)
  -> RenderingItem
  -> PipelineBuildDesc::fromRenderingItem(...)
  -> Vulkan pipeline preload
```

每帧阶段的主链路：

```text
update hook
  -> IGpuResource::setDirty()
  -> renderer.uploadData()
  -> renderer.draw()
```

## Scene、FrameGraph、Queue 各自只做一件事

| 对象 | 当前职责 | 不负责什么 |
|---|---|---|
| `Scene` | 持有 renderables、cameras、lights、scene-level resources | pipeline 构建细节 |
| `SceneNode` | 维护 transform、component、validated pass cache | 文件加载和 Vulkan 绑定 |
| `FrameGraph` | 描述一帧有哪些 pass 和 target | 资源 barrier/aliasing 的完整编译器，当前尚未实现 |
| `RenderQueue` | 把某个 pass 下可画对象整理成 `RenderingItem` | 临时猜测 mesh/material 是否匹配 |
| `RenderingItem` | backend 一次 draw 所需的稳定上下文 | 拥有 scene 或 material 生命周期 |

这里最关键的设计点是：`SceneNode` 在结构变化时重建 `ValidatedRenderablePassData`。`RenderQueue` 不再临时检查对象是否可画，而是消费已经验证过的 pass 数据。

## 资源生命周期统一走 IGpuResource

GPU 可消费资源都实现 `IGpuResource`。CPU 侧修改后标 dirty，backend 在上传阶段同步。

| 资源 | 当前用途 |
|---|---|
| `ParameterBuffer` | `MaterialInstance` 的 UBO / SSBO 字节缓冲 |
| `CombinedTextureSampler` | 材质 texture binding 的 GPU 资源 |
| `CameraData` | camera scene-level UBO |
| `DirectionalLightData` / `SceneLightsData` | light scene-level UBO |
| `PerDrawData` | 每个 draw 的 model matrix push constant payload |
| `SkeletonData` | 骨骼矩阵 GPU 数据 |

当前 draw push constant ABI 已收敛成 model-only：`PerDrawLayout` 只是 `PerDrawLayoutBase` 的别名，shader 侧只依赖 `model` 字段。

## Pipeline identity 来自对象和材质的结构签名

Pipeline 像一套已经调好的机器配置。它不能被每个参数值改变，否则缓存会失效；它应该只被结构性差异改变。

当前 `PipelineKey` 由 object/material 两侧签名组合而来：

| 来源 | 参与 pipeline identity 的信息 |
|---|---|
| Object side | mesh vertex input、topology、object signature |
| Material side | shader、variants、pass render state、descriptor layout 等 |

材质参数值、节点 transform、普通 UBO 数据不应该改变 pipeline identity。它们走 resource upload 或 push constant。

## Editor 是当前最完整的系统集成入口

`src/demos/lxe_editor/` 不是新渲染层，而是把当前架构串成可交互工作台：

| editor 部分 | 连接的系统 |
|---|---|
| project/template/scene document | 资产系统和场景系统 |
| SceneRuntime | `.scene.yaml` 到 runtime `Scene` |
| ImGui panels | Scene Tree、Inspector、Console、Toolbar |
| CommandBus | UI、Console、HTTP/WebSocket、recording 共用行为线 |
| API service | 状态快照、事件流、远程诊断 |

Web Editor、engine-level MCP/CLI/agent 还没有实现；它们属于 roadmap 和 pending requirements，不能当作当前架构事实。

## 继续阅读

- [项目目录结构](project-layout.md)
- [场景系统](../scene-system/index.md)
- [材质系统](../concepts/material/index.md)
- [Editor System](../design/editor-system/index.md)
