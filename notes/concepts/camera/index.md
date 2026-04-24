# 相机怎样进入一帧渲染

这篇文档讨论的重点不是“相机有哪些字段”，而是当前这套引擎怎样理解相机，以及相机资源是怎样进入 scene 与渲染路径的。

## 相机在这里首先是 scene-level 资源

当前项目里的相机系统是比较轻的。

`Camera` 本身主要保存三类东西：

- CPU 端的相机参数：`position`、`target`、`up`、投影参数
- GPU 可上传的 `CameraData`
- 可选的 `RenderTarget` 绑定关系

也就是说，这里的相机首先是一个 scene-level 资源对象，而不是一个自己会跑输入逻辑的 controller。

## 这套系统解决什么问题

相机系统解决的是“从哪里看场景、把哪份观察参数送进 shader、以及它服务哪个输出目标”这几个问题。

在当前实现里，它最直接的作用是：

- 决定 view / projection 矩阵
- 生成 `CameraData`
- 帮 scene 在 `(pass, target)` 维度上选出该用哪份 camera 资源

## 日常使用里的主路径

最常见的用法很简单：

1. 从 `Scene` 里拿一个 camera
2. 改 `position`、`target`、`up`、`aspect`
3. 调 `updateMatrices()`

例如：

```cpp
auto scene = Scene::create(nullptr);
auto camera = scene->getCameras().front();

camera->position = {0.0f, 0.0f, 3.0f};
camera->target = {0.0f, 0.0f, 0.0f};
camera->up = {0.0f, 1.0f, 0.0f};
camera->aspect = 800.0f / 600.0f;
camera->updateMatrices();
```

如果一个场景有多个输出目标，还可以调用 `setTarget(target)`，让这台相机服务某个特定 `RenderTarget`。

## 当前代码已经走到哪一步

相机本身已经有一套很明确的运行时语义：

- Perspective / Orthographic 两种投影都已存在
- camera 和 `RenderTarget` 的绑定关系也已接入 scene 资源过滤

但 controller 这条线还没落地。

所以当前的状态是：

- 相机数据对象和 target 过滤已经有了
- Orbit / FreeFly controller 还没有正式实现
- camera visibility layer / layer mask 也还没有

对应需求：

- [`REQ-015`](../../requirements/finished/015-orbit-camera-controller.md)
- [`REQ-016`](../../requirements/finished/016-freefly-camera-controller.md)
- [`REQ-026`](../../requirements/026-camera-visibility-layer-mask.md)

## 这条边界为什么重要

相机系统负责的是“观察参数”和“target 过滤”。

它不直接决定：

- 某个对象参加哪些 material pass
- pipeline 身份怎么组成
- scene 里有哪些 renderable

所以，当我们在代码里看到 `Scene::getSceneLevelResources(pass, target)` 时，可以把 camera 理解为：

它负责在当前 target 下提供一份 scene-level `CameraData`，而不是直接干预材质 pass。

## 往实现层再走一步

从底层看，这条链路很直接：

- `Camera` 维护自己的矩阵和 `CameraData`
- `Scene` 持有 `std::vector<CameraSharedPtr>`
- queue 构建前，scene 会按 `matchesTarget(target)` 收集命中的 camera 资源
- shader 如果声明了 `CameraUBO`，后续 descriptor 装配就会把这份 `CameraData` 接进去

继续展开时，可以参考：

- [camera.hpp](/home/lx/proj/renderer-demo/src/core/scene/camera.hpp:46)
- [`../../subsystems/scene.md`](../../subsystems/scene.md)
