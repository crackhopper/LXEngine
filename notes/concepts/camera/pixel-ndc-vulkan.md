# 一次点击怎样穿过像素、NDC 和 Vulkan

这页文档讲的是一条很具体的链路：我们在 editor 里点一个像素，这个位置怎样变成一条射线，再怎样回到屏幕上的一个像素。它的重点不是“Vulkan 坐标系百科”，而是当前代码里真正生效的那套约定。

可以把这条链路想成一张往返机票。出发站是 editor 里的鼠标像素，转机站是 NDC，落地站是世界空间里的点；如果我们再把这个点投回屏幕，它应该回到同一个像素。只要中间某一站偷偷换了地图，比如单独翻转一次 Y 轴，往返位置就会对不上。

## 这套约定为什么重要

当前引擎里，下面几条功能都依赖同一套屏幕语义：

| 功能 | 它依赖什么 | 如果约定不一致会发生什么 |
|---|---|---|
| scene picking | 像素到射线 | 点击命中会漂移 |
| `pick_debug` | 射线点回投影到屏幕 | 日志正确，但屏幕 marker 跑到镜像位置 |
| viewport overlay | 世界点投影到 editor 面板 | gizmo、选框、wire box 会和鼠标感觉不一致 |

所以我们现在把它当成一条统一规则，而不是每个模块自己解释一次。

## 当前代码采用的三层坐标

| 层级 | 当前约定 | 直觉类比 |
|---|---|---|
| 屏幕像素 | 左上角原点，`x` 向右，`y` 向下；整数像素表示像素中心 | 编辑器截图上的坐标尺 |
| NDC | `x` 向右，`y` 向上；屏幕中心是 `(0, 0)` | 镜头前的一张标准化玻璃板 |
| Vulkan viewport | `VkViewport{0, 0, width, height, 0, 1}`，不使用负高度翻转 | GPU 最终落屏时用的固定相框 |

这里最容易混淆的是第一层和第三层。屏幕像素是 editor 交互语义，Vulkan viewport 是 GPU 把 NDC 映到 framebuffer 的固定规则。我们现在要求它们相互兼容，但不允许 runtime 再插入一层“临时把整张画面上下镜像”的开关。

## 像素怎样变成 NDC

当前 pick 路径把整数像素当成像素中心，所以会先补上 `0.5` 再归一化：

```cpp
ndc.x = ((screenPixel.x + 0.5f) / viewportWidth) * 2.0f - 1.0f;
ndc.y = 1.0f - ((screenPixel.y + 0.5f) / viewportHeight) * 2.0f;
```

这段逻辑现在在：

```text
src/demos/lxe_editor/scene_interaction_controller.cpp
```

它表达的是一件很明确的事：屏幕像素的 `y` 向下增长，但 NDC 的 `y` 向上增长，所以这里会做一次显式反转。

## 世界点怎样回到屏幕像素

回投影时我们走相反方向：

```cpp
screenX = (ndc.x * 0.5f + 0.5f) * viewportSize.x - 0.5f;
screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y - 0.5f;
```

这里的 `-0.5` 和前面的 `+0.5` 是一组配套约定。前者表示“从像素中心坐标回到整数像素表达”，后者表示“从整数像素进入像素中心采样”。只要两边使用同一组约定，round-trip 就能闭合。

相关实现目前分布在：

| 代码位置 | 作用 |
|---|---|
| `src/demos/lxe_editor/scene_interaction_controller.cpp` | `pick_debug` 回投影 |
| `src/core/editor/viewport_overlay.cpp` | 选框、overlay 投影 |
| `src/core/scene/components/camera_component.cpp` | `pickRay()` 与 view/proj 矩阵 |

## Vulkan 这一层现在怎么处理

当前 Vulkan backend 固定输出：

```cpp
VkViewport{0.0f, 0.0f, width, height, 0.0f, 1.0f}
```

也就是说，我们不再让 runtime 根据环境变量把 `viewport.height` 变成负数。历史上确实存在一个排障变量 `LX_RENDER_FLIP_VIEWPORT_Y`，但它现在已经不属于当前运行时约定：

| 项目 | 当前状态 | 说明 |
|---|---|---|
| `LX_RENDER_FLIP_VIEWPORT_Y` | runtime 不再读取 | 它曾用于排查早期 Vulkan 画面上下颠倒 |
| editor / picking / overlay | 统一使用 top-left 屏幕语义 | 与 `pick_debug` 和实际显示保持一致 |
| Vulkan viewport | 固定正高度 | 不再允许把最终画面单独镜像一次 |

这意味着如果我们再看到“日志中的 `projectedPixel` 正确，但屏幕显示上下镜像”，问题就不应该再来自这个旧开关。

## 代码里怎样保证这件事不回退

现在有两类测试在守这条规则：

| 测试 | 守住什么 |
|---|---|
| `test_lxe_editor_interaction` | 像素 -> 射线 -> 射线上点 -> 回投影像素 的 round-trip |
| `test_vulkan_viewport_convention` | Vulkan viewport 固定 `y=0,height>0`，且不受旧环境变量影响 |

这两类测试一前一后，分别守住 CPU 屏幕数学和 GPU viewport 约定。

## 这页文档的边界

这页只解释当前 editor 交互和 Vulkan backend 共用的坐标约定。它不展开：

- mesh 表面求交算法
- render-to-texture 的 UV 朝向问题
- OpenGL / Direct3D / Metal 的跨 API 兼容策略

这些话题各自都值得单独展开，但不影响我们理解当前这条 click-to-pick 主路径。

## 继续阅读

- [`../../subsystems/vulkan-backend.md`](../../subsystems/vulkan-backend.md)
- [`../../subsystems/scene.md`](../../subsystems/scene.md)
- [src/demos/lxe_editor/scene_interaction_controller.cpp](/home/lixiang/proj/LXEngine/src/demos/lxe_editor/scene_interaction_controller.cpp:1)
- [src/core/scene/components/camera_component.cpp](/home/lixiang/proj/LXEngine/src/core/scene/components/camera_component.cpp:1)
