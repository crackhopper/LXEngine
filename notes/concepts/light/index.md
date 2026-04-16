# 光源系统

这篇文档面向引擎使用者，解释光源对象、scene-level 光照资源，以及当前项目里“已实现”和“尚未实现”的光照能力边界。

## 你会在什么场景接触它

当前代码里你真正会直接用到的光源类型只有 `DirectionalLight`。

典型场景有两类：

- 给前向渲染物体提供方向光参数。
- 按 pass 控制某个光源是否参与 `Forward`、`Deferred` 或 `Shadow`。

## 它负责什么

当前光源系统分两层：

- `LightBase`：抽象出 `getPassMask()`、`getUBO()`、`supportsPass(pass)` 这三个运行时入口。
- `DirectionalLight`：提供一份 `DirectionalLightUBO`。

因此，光源系统当前主要负责：

- 保存“这个光源参加哪些 pass”的掩码。
- 向 shader 提供 `LightUBO` 这份 scene-level 资源。

它不负责：

- 决定 material pass 的启停，这属于 [材质系统](../material/index.md)。
- 决定 pipeline 身份和构建，这属于 [渲染管线](../pipeline/index.md)。
- 提供完整的 IBL / 多光源聚合合同，这些能力还未收敛。

## 当前实现状态

- 已实现：`DirectionalLight`、`LightBase`、pass mask、scene-level light 资源收集。
- 部分实现：scene 可以持有多个 `LightBase`，但当前真正稳定的 shader / 资源合同仍以方向光为中心。
- 尚未实现：`SpotLight`，见 [`REQ-027`](../../requirements/027-spot-light.md)。
- 尚未实现：IBL 环境光资源接入，见 [`REQ-028`](../../requirements/028-ibl-environment-lighting.md)。
- 尚未实现：统一的多光源场景资源模型，见 [`REQ-029`](../../requirements/029-multi-light-scene-resource-model.md)。

## 常见使用方式

最常见的写法是从 scene 里取默认光源，然后直接改 `ubo->param`，最后调用 `setDirty()`：

- `dir` 是方向，类型是 `Vec4f`
- `color` 是颜色 / 强度组合，类型也是 `Vec4f`
- 如果要改 pass 参与范围，调用 `setPassMask(...)`

`Scene::getSceneLevelResources(pass, target)` 在收集 light 资源时只看 `supportsPass(pass)`，不看 render target。这和 [相机系统](../camera/index.md) 不同：camera 按 target 过滤，light 按 pass 过滤。

## 与其他概念的关系

- 和 [场景对象](../scene/index.md)：scene 持有 `std::vector<LightBasePtr>`，并把命中 pass 的 light UBO 追加到 item 的 descriptor resources 末尾。
- 和 [材质系统](../material/index.md)：如果 shader 声明了 `LightUBO`，材质 pass 就能消费光照参数。
- 和 [渲染管线](../pipeline/index.md)：light 本身不直接决定 `PipelineKey`，但会影响某个 pass 下 scene-level 资源的装配方式。

## 示例代码

```cpp
auto dirLight =
    std::dynamic_pointer_cast<DirectionalLight>(scene->getLights().front());

dirLight->ubo->param.dir = {0.0f, -1.0f, 0.0f, 0.0f};
dirLight->ubo->param.color = {1.0f, 1.0f, 1.0f, 1.0f};
dirLight->ubo->setDirty();
```

当前实现入口可以对照 [light.hpp](/home/lx/proj/renderer-demo/src/core/scene/light.hpp:17) 和 [`../../subsystems/scene.md`](../../subsystems/scene.md)。
