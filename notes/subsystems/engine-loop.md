# Engine Loop

> `EngineLoop` 是 renderer 之上的运行时编排层。它不关心 Vulkan 细节，也不负责决定 pass / pipeline 如何构建；它负责把“开始一个场景”和“执行一帧”组织成稳定、可复用的引擎入口。
>
> 如果你刚接触这个概念，先看 [`../引擎循环.md`](../引擎循环.md)。本文档更偏当前实现和接口行为。
>
> 对应需求归档：`docs/requirements/finished/020-engine-loop-and-scene-lifecycle.md`

## 它解决什么问题

在引入 `EngineLoop` 之前，应用层通常直接手写：

```cpp
while (running) {
  // update scene data
  renderer->uploadData();
  renderer->draw();
}
```

这能跑，但有三个问题：

- 场景启动和每帧执行容易混在一起看，进而误解 `initScene/buildFromScene/preloadPipelines` 是 per-frame 路径。
- 每个 demo / tutorial 都要自己决定 update hook 放在哪里，时间推进在什么时候发生。
- 如果同一个 loop 对象被复用，scene / clock / update hook 的生命周期没有统一 owner。

`EngineLoop` 把这些编排职责收敛起来：

- `startScene(scene)`：开始或重建一个场景
- `tickFrame()`：推进 clock，跑业务 update，然后 upload + draw
- `run()`：默认 while-loop 薄壳
- `stop()`：终止运行
- `requestSceneRebuild()`：显式请求结构性重建

## 核心接口

位置：`src/core/gpu/engine_loop.{hpp,cpp}`

```cpp
class EngineLoop {
public:
  using UpdateHook = std::function<void(Scene &, const Clock &)>;

  void initialize(WindowPtr window, RendererPtr renderer);
  void startScene(ScenePtr scene);
  void setUpdateHook(UpdateHook hook);
  void requestSceneRebuild();
  void tickFrame();
  void run();
  void stop();

  const Clock &getClock() const;
  ScenePtr getScene() const;
};
```

它持有或协调：

- `WindowPtr`
- `RendererPtr`
- `ScenePtr`
- `Clock`
- 当前 frame 的 update hook
- `running` / `rebuildRequested` 状态

## 运行顺序

### 开始一个场景

```text
EngineLoop::startScene(scene)
  ├── 保存 m_scene
  └── renderer->initScene(scene)
```

这里不会自己构建 `FrameGraph`；那仍然是 renderer 的职责。对于 Vulkan backend，`initScene` 内部会继续完成：

- swapchain target 派生
- camera target backfill
- `FrameGraph::buildFromScene(...)`
- pipeline preload

### 执行一帧

```text
EngineLoop::tickFrame()
  ├── 如有需要，先 rebuildSceneIfRequested()
  ├── clock.tick()
  ├── updateHook(scene, clock)
  ├── renderer->uploadData()
  └── renderer->draw()
```

这个顺序的重要性：

- `Clock` 必须先推进，业务层才能拿到当前 frame 的 `deltaTime/totalTime`
- 业务层必须先改 CPU 真值，再让 dirty 通道把改动上传到 GPU
- `draw()` 只消费本帧已经准备好的数据，不做 side-channel 更新

## 重建语义

`EngineLoop` 区分两类变化：

- **dirty-only**：只改 UBO / push constant / camera 矩阵 / 灯光参数，这类变化走 `updateHook -> uploadData -> draw`
- **structural**：会影响 scene 初始化产物的变化，例如新增 renderable、改变 pass 参与、改变 shader/material 结构

结构性变化不靠每帧隐式重建，而是显式调用：

```cpp
loop.requestSceneRebuild();
```

下一个 `tickFrame()` 会在 frame 开始时重新 `initScene(scene)` 一次。

## 和 Vulkan Backend 的边界

```text
App / Demo
  └── EngineLoop
        ├── Clock
        ├── update hook
        └── Renderer
              └── VulkanRenderer
                    ├── initScene
                    ├── uploadData
                    └── draw
```

边界规则：

- `EngineLoop` 负责**什么时候**开始场景、什么时候执行一帧
- `VulkanRenderer` 负责**如何**初始化 scene 对应的渲染结构，以及如何 upload/draw
- backend 不负责业务回调、主循环策略、通用生命周期管理

这也是为什么 `EngineLoop` 放在 `core/gpu/`，而不是 `backend/vulkan/`。

## 当前落地状态

已经完成的接入：

- `src/test/test_render_triangle.cpp` 已从手写 while-loop 改为 `EngineLoop`
- `src/test/integration/test_engine_loop.cpp` 锁定了以下行为：
  - `startScene()` 不是 per-frame
  - update hook 先于 `uploadData()/draw()`
  - `requestSceneRebuild()` 触发显式重建
  - `run()` 能被 `stop()` 或 window close 终止
  - `initialize()` 会重置旧的 runtime state

尚未接入但已对齐方向：

- `REQ-019 demo_scene_viewer`
- tutorial 示例的“推荐形状”

## 关联文档

- `notes/architecture.md`
- `notes/subsystems/vulkan-backend.md`
- `notes/tutorial/05-app-main.md`
- `docs/requirements/finished/020-engine-loop-and-scene-lifecycle.md`
