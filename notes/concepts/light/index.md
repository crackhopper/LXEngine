# 光照参数怎样进入场景

这篇文档讨论的重点不是一般性的光照理论，而是当前项目里的光源对象是什么、它们解决什么问题，以及光照参数怎样进入 scene 和 shader。

## 当前的光源骨架是什么

现在这套光源系统还比较朴素。

当前真正可直接使用的光源类型只有 `DirectionalLight`。它建立在一个更抽象的基类之上：

- `LightBase`
- `DirectionalLight`

`LightBase` 负责统一入口，例如 `getUBO()`、`supportsPass(pass)`；`DirectionalLight` 则给出一份具体的 `DirectionalLightData`。

所以，这个系统现在更接近“方向光 + scene-level light resource”的最小骨架。

## 它解决什么问题

光源系统解决的是“把光照参数稳定地送进场景和 shader”这个问题。

在当前实现里，它最直接的作用是：

- 保存光源方向、颜色 / 强度
- 控制这个光源参加哪些 pass
- 在 queue 构建时，把 `LightUBO` 当作 scene-level 资源追加到 draw 输入中

## 日常使用里的主路径

最常见的路径是从 scene 里拿默认方向光，然后直接改参数：

```cpp
auto dirLight =
    std::dynamic_pointer_cast<DirectionalLight>(scene->getLights().front());

dirLight->ubo->param.dir = {0.0f, -1.0f, 0.0f, 0.0f};
dirLight->ubo->param.color = {1.0f, 1.0f, 1.0f, 1.0f};
dirLight->ubo->setDirty();
```

如果要控制它参加哪些 pass，可以改 `passMask`。当前 scene 在收集 light 资源时只看 `supportsPass(pass)`，不看 `RenderTarget`。

## 当前代码已经走到哪一步

这套系统已经能稳定支撑“scene 里有方向光，shader 能消费 light UBO”这条主路径。

但它还远没到“完整光照系统”的阶段。

现在的状态可以理解成：

- 已有：`DirectionalLight`、`LightBase`、scene-level light resource、pass mask
- 部分有：scene 可以持有多个 light object，但完整的多光源 shader 合同还没收口
- 还没有：`SpotLight`
- 还没有：IBL 环境光资源接入
- 还没有：正式的多光源资源模型

对应需求：

- [`REQ-027`](../../requirements/027-spot-light.md)
- [`REQ-028`](../../requirements/028-ibl-environment-lighting.md)
- [`REQ-029`](../../requirements/029-multi-light-scene-resource-model.md)

## 这条边界为什么重要

当前光源系统负责的是 scene-level 光照资源，不是材质系统的替代，也不是 pipeline 身份的直接来源。

换句话说：

- 材质决定“某个 pass 要不要使用光照，以及怎么使用”
- 光源系统提供“当前 scene 里有哪些光照参数可以被消费”
- 渲染管线再把这些资源和 pass 输入整理成真正的 draw 上下文

## 往实现层再走一步

从实现上看，链路是这样的：

- `Scene` 持有 `std::vector<LightBaseSharedPtr>`
- queue 构建时，scene 会把命中当前 pass 的 light UBO 收集出来
- shader 如果声明了 `LightUBO`，这份资源就会在 descriptor 组装时被接进去

继续展开时，可以参考：

- [light.hpp](/home/lx/proj/renderer-demo/src/core/scene/light.hpp:17)
- [`../../subsystems/scene.md`](../../subsystems/scene.md)
