# 光源对象

这篇文档面向引擎使用者，解释光源对象在场景中的职责、常见使用方式，以及它与材质、阴影和场景照明之间的关系。

## 你会在什么场景接触它

当前代码里你真正会直接用到的光源类型只有 `DirectionalLight`。

典型场景有两类：

- 给前向渲染物体提供方向光参数。
- 按 pass 控制某个光源是否参与 `Forward`、`Deferred` 或 `Shadow`。

和 camera 一样，`Scene` 构造时会自动放进一个默认 directional light，所以 demo 和测试里通常直接从 `scene->getLights().front()` 取出来改参数。

## 它负责什么

当前实现里的光源体系分两层：

- `LightBase`：抽象出 `getPassMask()`、`getUBO()`、`supportsPass(pass)` 这三个运行时入口。
- `DirectionalLight`：提供一份 `DirectionalLightUBO`，里面只有 `dir` 和 `color`。

因此，光源对象当前主要负责：

- 保存“这个光源参加哪些 pass”的掩码。
- 向 shader 提供 `LightUBO` 这份 scene-level 资源。

它目前不负责阴影贴图、光源列表聚合、衰减模型或点光/聚光。项目现状就是一个比较朴素的方向光资源对象。

## 常见使用方式

最常见的写法是从 scene 里取默认光源，然后直接改 `ubo->param`，最后调用 `setDirty()`：

- `dir` 是方向，类型是 `Vec4f`
- `color` 是颜色/强度组合，类型也是 `Vec4f`
- 如果要改 pass 参与范围，调用 `setPassMask(...)`

`Scene::getSceneLevelResources(pass, target)` 在收集 light 资源时只看 `supportsPass(pass)`，不看 render target。这和 camera 不同：camera 按 target 过滤，light 按 pass 过滤。

## 与其他概念的关系

- 和 `Scene`：scene 持有 `std::vector<LightBasePtr>`，渲染队列构建时会把命中 pass 的 light UBO 追加到每个 item 的 descriptor resources 后面。
- 和 `Material` / shader：如果 shader 声明了 `LightUBO`，材质 pass 就能消费光照参数；是否真的用光，还取决于材质 variant，例如 `blinnphong_0` 的 `USE_LIGHTING`。
- 和阴影：目前只是 pass mask 上可以包含 `Shadow`，但这里的概念文档不能把它写成“已有完整阴影系统”，因为现有 light 对象本身没有 shadow map 资源。

## 示例代码

```cpp
auto dirLight =
    std::dynamic_pointer_cast<DirectionalLight>(scene->getLights().front());

dirLight->ubo->param.dir = {0.0f, -1.0f, 0.0f, 0.0f};
dirLight->ubo->param.color = {1.0f, 1.0f, 1.0f, 1.0f};
dirLight->ubo->setDirty();
```

可以对照 [light.hpp](/home/lx/proj/renderer-demo/src/core/scene/light.hpp:17) 看抽象层接口，也可以看 [test_render_triangle.cpp](/home/lx/proj/renderer-demo/src/test/test_render_triangle.cpp:72) 里的实际更新方式。
