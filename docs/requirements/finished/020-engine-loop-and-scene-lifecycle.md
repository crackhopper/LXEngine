# REQ-020: EngineLoop 与场景生命周期

## 背景

当前 renderer 的真实代码路径已经把“场景初始化”和“每帧渲染”分开了：

- `Renderer::initScene(scene)` 是一次性的场景接管入口
- `Renderer::uploadData()` / `Renderer::draw()` 才是每帧调用

`src/backend/vulkan/vulkan_renderer.cpp` 里，`FrameGraph::buildFromScene(...)` 与 `preloadPipelines(...)` 只发生在 `initScene()` 中，不在 `uploadData()` / `draw()` 中重复调用。

但文档层目前仍有两个问题：

1. `notes/architecture.md` 把 `initScene -> buildFromScene -> preloadPipelines -> uploadData -> draw` 串成“**一帧的数据流**”，容易让读者误解为这些步骤都属于 per-frame。
2. demo / tutorial / requirement 示例仍普遍写成业务代码直接持有 `Window + Renderer + while(running) { ... }`。这样虽然能跑，但会把“场景开始时要做什么”“每帧钩子在什么位置”“何时允许重建场景结构”散落到各个调用点里。

随着 REQ-014~019 已经引入 `Clock`、input、camera controller、debug UI，这些能力开始形成一个完整的运行时编排问题。继续把这些职责留在 backend 或 demo `main.cpp` 里，会使引擎对外接口越来越难用。

因此，本 REQ 引入 `EngineLoop` 概念，作为 **renderer 之上的编排层**：

- backend 继续只负责“上传 GPU 数据并执行绘制”
- `EngineLoop` 负责“开始一个场景、每帧驱动业务更新、调用 renderer 执行 upload + draw”

## 目标

1. 明确区分“场景初始化阶段”和“每帧执行阶段”
2. 引入 `EngineLoop` 作为引擎对外的统一运行入口
3. 规定业务层更新钩子发生在 `upload dirty` 之前
4. 规定 backend **不**负责时钟推进、业务回调、场景生命周期编排
5. 为后续 demo / tutorial / 更高层 gameplay API 提供稳定骨架

## 需求

### R1: 场景初始化与每帧循环必须显式分离

引擎文档、接口命名与示例必须把以下两类阶段明确区分：

**A. 场景启动阶段（once per scene start / rebuild）**

```cpp
engineLoop.startScene(scene);
// 内部典型顺序：
//   renderer->initScene(scene)
//   frameGraph.buildFromScene(scene)
//   preloadPipelines(...)
```

**B. 每帧执行阶段（once per frame）**

```cpp
engineLoop.tickFrame();
// 内部典型顺序：
//   clock.tick()
//   user update hook(...)
//   renderer->uploadData()
//   renderer->draw()
```

文档中不得再把 `buildFromScene(...)`、`collectAllPipelineBuildDescs()`、`preloadPipelines(...)` 描述为“每帧都调用”的常规路径。

### R2: 引入 `EngineLoop` 作为 renderer 之上的编排层

新增一个高于 `gpu::Renderer` 的运行时抽象，命名为 `EngineLoop`。

它的职责是：

- 持有或协调 `Window`
- 持有或协调 `Renderer`
- 持有 `Clock`
- 管理当前 `Scene`
- 暴露“开始场景”和“驱动一帧”的统一接口

示意接口：

```cpp
class EngineLoop {
public:
  void initialize(WindowPtr window, RendererPtr renderer);
  void startScene(ScenePtr scene);
  void run();
  void tickFrame();
  void stop();
};
```

本 REQ 先定义生命周期与职责边界；具体 API 名称、回调签名、所有权形式可在 proposal/design 阶段细化。

### R3: `EngineLoop` 必须提供业务层每帧更新钩子

`EngineLoop` 必须定义一个位于 render 前的更新阶段，让业务层有机会修改场景数据、相机、灯光、材质参数等 CPU 侧真值。

顺序必须满足：

```cpp
clock.tick();
userUpdate(scene, clock, input /* optional */);
renderer->uploadData();
renderer->draw();
```

设计约束：

- 业务钩子发生在 `uploadData()` 之前
- 业务钩子修改 `IRenderResource` 后，依赖 dirty 通道进入 GPU
- `draw()` 前不允许再插入“会绕过 dirty 模型”的 side-channel 上传逻辑

### R4: backend 不负责 EngineLoop 级别的编排职责

`backend/vulkan` 的职责边界必须保持在“渲染执行器”：

- 可以接管 scene 并建立渲染所需的 `FrameGraph` / pipeline cache
- 可以在每帧同步 dirty 资源并提交 draw
- **不负责**时钟推进
- **不负责**业务层 update callback
- **不负责**窗口主循环策略
- **不负责**通用 gameplay / app 生命周期

换句话说：

- `VulkanRenderer::initScene()` 是 renderer 的场景接管入口
- `EngineLoop::startScene()` 才是引擎面对应用层的场景开始入口

### R5: 结构性场景变化必须通过显式重建入口处理

会影响 `FrameGraph` / `RenderQueue` / pipeline preload 结果的变化，不应通过“每帧隐式重建整个 scene”解决。

这类变化包括但不限于：

- 新增或删除 renderable
- renderable 的 pass 能力变化
- material / shader 导致 `PipelineKey` 结构变化
- camera target / pass 配置变化
- pass 图结构变化

对于这类变化，系统必须提供**显式**的 scene rebuild 语义，例如：

```cpp
engineLoop.requestSceneRebuild();
// 或
engineLoop.startScene(scene); // restart current scene
```

本 REQ 不强制具体 API，但要求设计文档明确：

- 哪些变化只需要 dirty upload
- 哪些变化需要重建 scene/render graph

### R6: Architecture 文档必须改成“两段工作流”

`notes/architecture.md` 必须把当前“`一帧的数据流`”拆分为至少两段：

1. **开始渲染一个场景时**
2. **每帧循环时**

推荐工作流：

```text
开始渲染一个场景：
  1. 初始化场景
  2. 构建 FrameGraph / RenderQueue
  3. 预构建 pipelines

每帧循环：
  1. 推进 clock
  2. 调业务层钩子，允许修改 scene 数据
  3. upload dirty resources
  4. 执行 draw
```

### R7: `EngineLoop` 作为前置需求进入依赖链

后续依赖主循环形状的需求文档，必须以 `REQ-020` 作为前置上下文或直接依赖。

至少包括：

- `REQ-019 demo_scene_viewer`
- tutorial 中对应用主循环的描述

若其他 requirement 仍保留直接手写 `while(running) { uploadData(); draw(); }` 的示例，应在相关段落补充说明那是“临时展开版”，长期归宿是 `EngineLoop`。

## 修改范围

| 文件 | 改动 |
|---|---|
| `docs/requirements/finished/020-engine-loop-and-scene-lifecycle.md` | 新增 |
| `notes/architecture.md` | 把初始化期 / 每帧期拆开描述 |
| `notes/tutorial/05-app-main.md` | 主流程改写为 `EngineLoop` 心智模型 |
| `docs/requirements/014-clock-and-delta-time.md` | 下游说明改为服务 `EngineLoop` |
| `src/test/test_render_triangle.cpp` | 当前 runnable 入口切换为 `EngineLoop` |
| `docs/requirements/019-demo-scene-viewer.md` | 需求方向改为基于 `EngineLoop` |

## 边界与约束

- **本 REQ 不直接实现** gameplay ECS / scripting / physics loop
- **本 REQ 不要求** backend 立即移除 `initScene()`；相反，它保留 renderer 自己的场景接管能力
- **本 REQ 不规定** fixed update / variable update / interpolation 的完整策略；那是更完整 runtime 框架的后续需求
- **本 REQ 不要求**所有旧 demo 立刻迁移，只要求新文档与后续 proposal 不再建立在错误的 per-frame 初始化认知上

## 依赖

- **REQ-014**：`Clock`
- **REQ-012/013**：输入系统（若 `EngineLoop` 要统一驱动 input polling）
- renderer 当前已有的 `initScene()` / `uploadData()` / `draw()` 三段式接口

## 下游

- 当前实现：`test_render_triangle` 已作为首个 runnable entry 接入 `EngineLoop`
- **REQ-019**：`demo_scene_viewer` 仍是后续更完整的集成入口
- tutorial：不再让用户把 backend 当成“整个引擎入口”
- 后续 gameplay / app framework / editor 需求：统一落在 `EngineLoop` 之上

## 实施状态

已完成。
