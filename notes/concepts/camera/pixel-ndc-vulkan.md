# 相机投影怎样连接 OpenGL 语义和 Vulkan 后端

这页文档讲的是当前引擎里相机坐标、投影矩阵、editor 像素和 Vulkan viewport 怎样接起来。可以把它想成一副可更换镜片的相机：相机机身、场景建模、shader 里的观察空间都按 OpenGL 风格理解；真正装到不同图形后端前，我们只更换最后的投影镜片。

这条规则的目标很直接：建模和交互数学保持右手系、Y 向上、相机看向 `-Z`；渲染后端需要 Vulkan 时，投影矩阵自己负责把结果变成 Vulkan clip / NDC 需要的形式。

## 当前统一约定

| 层级 | 当前约定 | 代码入口 |
|---|---|---|
| world | 右手系，`+Y` 向上 | `SceneNode` / `Transform` |
| view | 右手系，`+Y` 向上，相机朝 `-Z` 看 | `CameraComponent::getViewMatrix()` |
| editor 像素 | 左上角原点，`x` 向右，`y` 向下 | SDL / ImGui / editor viewport |
| OpenGL 投影语义 | `+Y` 向上，深度 `-1..1` | `getProjMatrix(..., GraphicsAPI::OpenGL)` |
| Vulkan 投影语义 | 投影矩阵翻转 `Y`，深度 `0..1` | `getProjMatrix(..., GraphicsAPI::Vulkan)` |
| Vulkan viewport | 正高度 `VkViewport{0, 0, width, height, 0, 1}` | `makeVulkanViewport()` |

这里最重要的是职责边界：**Vulkan 的 Y 翻转属于投影矩阵，不属于 viewport**。这样相机组件可以按 backend 枚举生成正确矩阵，Vulkan command buffer 不再承担坐标系修正。

## 像素和 editor 交互仍然按 OpenGL 语义理解

Editor 里的 picking、box selection、gizmo 和 debug 投影都不是 GPU 落屏步骤，它们处理的是用户看到的面板像素。因此它们继续使用“屏幕上方对应正 `ndc.y`”的 OpenGL 风格交互语义：

```cpp
ndc.x = ((screenPixel.x + 0.5f) / viewportWidth) * 2.0f - 1.0f;
ndc.y = 1.0f - ((screenPixel.y + 0.5f) / viewportHeight) * 2.0f;
```

回投影时也走相反方向：

```cpp
screenX = (ndc.x * 0.5f + 0.5f) * viewportSize.x - 0.5f;
screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y - 0.5f;
```

这组公式和 `GraphicsAPI::OpenGL` 投影矩阵配套使用。它不表示 Vulkan 也采用 OpenGL depth，而是表示 CPU 侧 editor 交互仍然在同一个“Y 向上”的逻辑玻璃板上做往返计算。

## 渲染提交使用 backend 投影矩阵

`CameraComponent::getProjMatrix(...)` 现在接受 `GraphicsAPI`：

| API | `Y` 处理 | 深度范围 | 典型调用 |
|---|---|---|---|
| `GraphicsAPI::OpenGL` | 保持 `+Y` 向上 | `-1..1` | picking、overlay、gizmo、debug project |
| `GraphicsAPI::Vulkan` | 投影矩阵内翻转 `Y` | `0..1` | camera UBO / Vulkan draw path |

渲染路径里，`CameraComponent::updateMatrices()` 默认写入 Vulkan 投影矩阵，因为当前运行后端是 Vulkan。CPU 交互路径在需要投影/反投影时显式请求 `GraphicsAPI::OpenGL`，避免把后端落屏约定混进 editor 屏幕数学。

## Y 到底翻几次

渲染主路径只翻一次：

| 步骤 | 是否翻转 Y | 原因 |
|---|---|---|
| `world -> view` | 否 | 相机数学保持 OpenGL 风格右手系 |
| `view -> clip` with Vulkan projection | 是 | Vulkan 后端投影矩阵把 clip `Y` 翻到 Vulkan 需要的方向 |
| `ndc -> framebuffer` via viewport | 否 | Vulkan viewport 使用正高度，不再做第二次翻转 |

如果画面上下颠倒，优先检查是否有路径绕过了 `getProjMatrix(..., GraphicsAPI::Vulkan)`，或者又在 viewport / shader / CPU 投影里加了额外 Y 翻转。

## 代码里怎样防止回退

| 测试 | 守住什么 |
|---|---|
| `test_math` | OpenGL 和 Vulkan 投影矩阵的 Y 符号、深度范围不同 |
| `test_picking` | camera 默认投影是 Vulkan，editor picking 仍按相机姿态生成射线 |
| `test_vulkan_viewport_convention` | Vulkan viewport 使用正高度，Y 翻转不在 viewport 中发生 |
| `test_lxe_editor_interaction` | 像素、NDC、射线、回投影的 editor round-trip |

## 继续阅读

- [`../../subsystems/vulkan-backend.md`](../../subsystems/vulkan-backend.md)
- [`../../subsystems/scene.md`](../../subsystems/scene.md)
- `src/core/scene/components/camera_component.cpp`
- `src/backend/vulkan/details/commands/command_buffer.cpp`
