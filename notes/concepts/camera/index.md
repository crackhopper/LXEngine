# 相机怎样进入一帧渲染

这篇文档讨论的重点不是“相机有哪些字段”，而是当前这套引擎怎样理解相机，以及相机资源是怎样进入 scene 与渲染路径的。

## 相机在这里首先是 scene-level component

当前项目里的相机是 `CameraComponent`。它挂在 `SceneNode` 上，观察位置和朝向来自 owner 节点的 transform chain，投影参数和 GPU 可上传的 `CameraData` 由 component 自己维护。

也就是说，相机首先是一个 scene-level 资源 component，不是一个直接拥有输入逻辑的对象。Orbit / FreeFly controller 会写 owner `SceneNode` 的 transform，再由 `CameraComponent` 推导 view / projection。

## 这套系统解决什么问题

相机系统解决的是“从哪里看场景、把哪份观察参数送进 shader、以及它服务哪个输出目标”这几个问题。

在当前实现里，它最直接的作用是：

- 决定 view / projection 矩阵
- 生成 `CameraData`
- 帮 scene / render queue 在 `(pass, target)` 与 visibility mask 维度上选出该用哪份 camera 资源

## 日常使用里的主路径

最常见的用法是：

1. 创建或取得一个带 `CameraComponent` 的 `SceneNode`
2. 改节点 transform，或让 Orbit / FreeFly controller 改节点 transform
3. 设置 camera component 的投影参数、target 和 culling mask

例如：

```cpp
auto scene = Scene::create(nullptr);
auto cameraNode = SceneNode::create("main-camera");
auto camera = CameraComponent::create();
cameraNode->addComponent(camera);
scene->addCamera(cameraNode);

camera->aspect = 800.0f / 600.0f;
cameraNode->setTranslation({0.0f, 0.0f, 3.0f});
```

如果一个场景有多个输出目标，还可以调用 `setTarget(target)`，让这台相机服务某个特定 `RenderTarget`。如果相机只应该看见某些层，设置 `cullingMask`；debug overlay 走 `Layer_EditorOverlay`，普通游戏相机默认不包含这个层。

## 当前代码已经走到哪一步

相机已经有一套明确的运行时语义：

- Perspective / Orthographic 两种投影都已存在
- camera 和 `RenderTarget` 的绑定关系已经接入 scene 资源过滤
- Orbit / FreeFly controller 已经接入 `CameraComponent`
- camera culling mask 与 `SceneNode` visibility layer 已经接入 queue 构建

这条链路把“相机数据”“输入控制”和“可见性过滤”分开：相机 component 提供观察参数，controller 改 transform，queue 用 target / mask 决定它是否参与某个 pass。

## 这条边界为什么重要

相机系统负责的是“观察参数”和“target / visibility 过滤”。

它不直接决定：

- 某个对象参加哪些 material pass
- pipeline 身份怎么组成
- scene 里有哪些 renderable

所以，当我们在代码里看到 `Scene::getSceneLevelResources(pass, target)` 时，可以把 camera 理解为：

它负责在当前 target 下提供一份 scene-level `CameraData`，而不是直接干预材质 pass。

## 往实现层再走一步

从底层看，这条链路很直接：

- `CameraComponent` 维护投影参数和 `CameraData`
- owner `SceneNode` 提供 world transform
- `Scene` 持有 camera node 列表
- queue 构建前，scene 会按 `matchesTarget(target)` 和 camera culling mask 收集命中的 camera 资源
- shader 如果声明了 `CameraUBO`，后续 descriptor 装配就会把这份 `CameraData` 接进去

继续展开时，可以参考：

- [camera_component.hpp](/home/lixiang/proj/LXEngine/src/core/scene/components/camera_component.hpp:1)
- [camera_controller.hpp](/home/lixiang/proj/LXEngine/src/core/scene/camera_controller.hpp:1)
- [`pixel-ndc-vulkan.md`](./pixel-ndc-vulkan.md)
- [`../../subsystems/scene.md`](../../subsystems/scene.md)
