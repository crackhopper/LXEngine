# 相机系统

这篇文档面向引擎使用者，解释相机对象、投影模式、渲染目标，以及相机如何参与 scene-level 资源收集。

## 你会在什么场景接触它

你通常会在三种地方直接碰到相机系统：

- 场景初始化后，从 `Scene::getCameras()` 里拿到默认相机，或者自己 `scene->addCamera(...)`。
- 每帧 update hook 里修改相机位置、朝向、投影参数，然后调用 `updateMatrices()`。
- 当一个场景存在多个输出目标时，用 `setTarget(target)` 指定这台相机服务哪个 `RenderTarget`。

## 它负责什么

当前相机系统主要负责：

- 保存 CPU 端相机参数，例如 position、target、up、Perspective / Orthographic 投影参数。
- 维护一份 GPU 可上传的 `CameraUBO`。
- 可选绑定一个 `RenderTarget`，告诉 scene 过滤逻辑“这台相机服务哪个输出目标”。

它不负责：

- 决定对象参加哪些 material pass。
- 构建 pipeline 或决定 `PipelineKey`。
- 自动驱动控制器更新，这属于上层运行时编排。

## 当前实现状态

- 已实现：Perspective / Orthographic 两种投影模式。
- 已实现：camera target 与 `RenderTarget` 绑定，scene 会按 `(pass, target)` 收集命中的 `CameraUBO`。
- 尚未实现：Orbit / FreeFly 控制器本体，见 [`REQ-015`](../../requirements/015-orbit-camera-controller.md) 和 [`REQ-016`](../../requirements/016-freefly-camera-controller.md)。
- 尚未实现：camera visibility layer / layer mask，见 [`REQ-026`](../../requirements/026-camera-visibility-layer-mask.md)。

## 常见使用方式

最常见的路径是：

1. 拿到相机对象。
2. 修改 `position/target/up/aspect` 等字段。
3. 调用 `updateMatrices()`。
4. 如果相机要输出到特定 target，额外调用 `setTarget(target)`。

当前 `Scene::getSceneLevelResources(pass, target)` 只会挑选 `matchesTarget(target)` 的相机 UBO。注意这里的 target 过滤和 material pass 是两条线：相机决定“哪个输出目标使用这份 camera 资源”，并不直接决定材质 pass。后者请看 [渲染管线](../pipeline/index.md)。

## 与其他概念的关系

- 和 [场景对象](../scene/index.md)：scene 持有 `std::vector<CameraPtr>`，并把命中 target 的 `CameraUBO` 加到 scene-level 资源列表。
- 和 [材质系统](../material/index.md)：如果 shader 声明了 `CameraUBO`，材质 pass 就会消费这份 scene-level 资源。
- 和 [渲染管线](../pipeline/index.md)：camera 不参与 `PipelineKey`，但会影响某个 `(pass, target)` 组合下的 queue 构建结果。

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

如果你想看当前实现细节，可以直接看 [camera.hpp](/home/lx/proj/renderer-demo/src/core/scene/camera.hpp:46) 和 [`../../subsystems/scene.md`](../../subsystems/scene.md)。
