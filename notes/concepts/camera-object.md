# 相机对象

这篇文档面向引擎使用者，解释相机对象在场景中的职责、常见使用方式，以及它与场景、窗口和渲染输出之间的关系。

## 你会在什么场景接触它

你通常会在两种地方直接碰到 `Camera`：

- 场景初始化后，从 `Scene::getCameras()` 里拿到默认相机，或者自己 `scene->addCamera(...)`。
- 每帧 update hook 里修改相机位置、朝向、投影参数，然后调用 `updateMatrices()` 把 CPU 状态写回 `CameraUBO`。

当前项目里，`Scene` 构造时会自动塞进一个默认相机，因此很多 demo 和测试并不会显式 new 一个 camera，直接取 `scene->getCameras().front()` 即可。

## 它负责什么

当前实现里的 `Camera` 很轻量，主要负责三件事：

- 保存相机的 CPU 参数：`position`、`target`、`up`、透视或正交投影参数。
- 维护一份 GPU 可上传的 `CameraUBO`，字段是 `view`、`proj`、`eyePos`。
- 可选地绑定一个 `RenderTarget`，告诉 scene/resource 过滤逻辑“这台相机是给哪个输出目标服务的”。

它不负责：

- 驱动主循环，这属于 `EngineLoop`。
- 构建 draw item 或 pipeline，这属于 `SceneNode`、`RenderQueue` 和 backend。
- 自动更新矩阵。你改完相机参数后，要自己调用 `updateMatrices()`。

## 常见使用方式

最常见的路径是：

1. 先拿到相机对象。
2. 修改 `position/target/up/aspect` 等字段。
3. 调用 `updateMatrices()`。

如果相机要输出到特定 render target，还要额外调用 `setTarget(target)`。当前 `Scene::getSceneLevelResources(pass, target)` 只会挑选 `matchesTarget(target)` 的相机 UBO；没有 target 的相机不会命中任何具体 target。实际运行时，`VulkanRenderer::initScene` 会把“未指定 target 的相机”回填成 swapchain target。

## 与其他概念的关系

- 和 `Scene`：`Scene` 持有 `std::vector<CameraPtr>`，scene-level descriptor 资源收集时会把命中 target 的 `CameraUBO` 放进资源列表。
- 和 `EngineLoop`：通常在每帧 `updateHook` 里更新相机参数，再调用 `renderer->uploadData()` / `draw()`。
- 和 `RenderTarget`：camera 按 target 过滤，不按 pass 过滤。
- 和 shader：shader 里如果声明了 `CameraUBO`，backend 会按 binding name 把这份资源接到 descriptor 上。

## 示例代码

```cpp
auto scene = Scene::create(nullptr);
auto camera = scene->getCameras().front();

camera->position = {0.0f, 0.0f, 3.0f};
camera->target = {0.0f, 0.0f, 0.0f};
camera->up = {0.0f, 1.0f, 0.0f};
camera->aspect = 800.0f / 600.0f;
camera->updateMatrices();
```

如果你想看当前项目里最接近真实使用的例子，可以直接看 [test_render_triangle.cpp](/home/lx/proj/renderer-demo/src/test/test_render_triangle.cpp:70) 里对默认 camera 的更新方式，以及 [camera.hpp](/home/lx/proj/renderer-demo/src/core/scene/camera.hpp:46) 里的字段和 `updateMatrices()` 行为。
