# REQ-018: Debug UI helper（封装常用 ImGui widget）

## 背景

`REQ-017` 完成后，`VulkanRenderer` 会提供一个基于 ImGui 的 overlay UI 回调入口。到那时，上层 demo 可以直接在 `setDrawUiCallback()` 里调用原始 ImGui API，但如果每个 demo 都从 `ImGui::Begin()` / `SliderFloat()` / `ColorEdit3()` 开始手写：

- `Vec3f` / `Vec4f` / `StringID` 这类引擎类型每次都要做一层桥接
- 常见的调试面板，例如 FPS、相机参数、方向光参数，会在多个 demo 间重复
- panel 位置、默认大小、命名风格会逐渐失去统一

这条需求的目标不是再造一个 GUI 系统，而是在 `infra/gui/` 下提供一组足够薄的 helper，让 demo 代码更短、风格更一致，同时不阻止调用方直接使用原生 ImGui。

2026-04-16 按当前仓库核查：

- `src/infra/gui/` 当前只有 [gui.hpp](../../src/infra/gui/gui.hpp) 与 [imgui_gui.cpp](../../src/infra/gui/imgui_gui.cpp)
- [src/infra/CMakeLists.txt](../../src/infra/CMakeLists.txt) 是真实的 infra 构建入口，不存在 `src/infra/gui/CMakeLists.txt`
- 测试注册点是 [src/test/CMakeLists.txt](../../src/test/CMakeLists.txt)，不是 `src/test/integration/CMakeLists.txt`
- 现有材质系统的正式类型是 [MaterialInstance](../../src/core/asset/material_instance.hpp) / [MaterialTemplate](../../src/core/asset/material_template.hpp)，不是一个简单的 `Material`

基于这些现实约束，本 REQ 只要求交付一组“薄封装 helper + 常见 panel 组合函数”，不把它扩张成自动反射编辑器。

## 目标

1. 提供 `Vec3f` / `Vec4f` / `StringID` 与 ImGui 的桥接函数
2. 提供统一的 panel 容器入口
3. 提供 `Clock` / `Camera` / `DirectionalLight` 的常用调试 panel
4. helper 保持无状态、可组合、可与原生 ImGui 混用
5. helper 放在 `infra/gui/`，不把 ImGui 依赖泄漏到 `core`

## 需求

### R1: 新增 `debug_ui` helper 模块

新建：

- `src/infra/gui/debug_ui.hpp`
- `src/infra/gui/debug_ui.cpp`

命名空间使用：

```cpp
namespace LX_infra::debug_ui {
}
```

原因：

- helper 直接依赖 ImGui，不能放进 `LX_core`
- helper 只是 `infra/gui` 的一个薄工具层，不是新的 subsystem

### R2: 基础类型桥接函数

`debug_ui.hpp` 至少提供以下 helper：

```cpp
#pragma once

#include "core/math/vec.hpp"
#include "core/utils/string_table.hpp"

namespace LX_infra::debug_ui {

bool dragVec3(const char* label, LX_core::Vec3f& value,
              float speed = 0.01f, float min = 0.0f, float max = 0.0f);

bool dragVec4(const char* label, LX_core::Vec4f& value,
              float speed = 0.01f, float min = 0.0f, float max = 0.0f);

bool sliderFloat(const char* label, float& value, float min, float max);
bool sliderInt(const char* label, int& value, int min, int max);

bool colorEdit3(const char* label, LX_core::Vec3f& rgb);
bool colorEdit4(const char* label, LX_core::Vec4f& rgba);

void labelText(const char* label, const char* value);
void labelText(const char* label, const std::string& value);
void labelFloat(const char* label, float value);
void labelInt(const char* label, int value);
void labelStringId(const char* label, LX_core::StringID value);

} // namespace LX_infra::debug_ui
```

实现要求：

- 本质上都是 ImGui thin wrapper
- `Vec3f` / `Vec4f` 可按项目当前 math struct 布局直接桥接；如实现依赖连续 `float` 内存布局，可加静态断言保护
- `labelStringId()` 应通过 `GlobalStringTable::get().getName(value.id)` 或等价方式输出可读文本

### R3: Panel 容器 helper

新增统一的 panel 容器入口：

```cpp
namespace LX_infra::debug_ui {

bool beginPanel(const char* title);
void endPanel();

bool beginSection(const char* title);
void endSection();

void separatorText(const char* label);

} // namespace LX_infra::debug_ui
```

约束：

- `beginPanel()` 负责设置一致的默认位置 / 大小策略
- `endPanel()` 必须始终与 `beginPanel()` 配对
- `beginSection()` 可以是 `CollapsingHeader`、`TreeNode` 或等价折叠组，但对调用方保持统一入口

本 REQ 不要求封装更复杂的布局系统，例如 docking、表格 DSL 或响应式 panel 描述器。

### R4: 常见调试 panel

在 `debug_ui` 中提供以下组合函数：

```cpp
namespace LX_infra::debug_ui {

void renderStatsPanel(const LX_core::Clock& clock);

void cameraPanel(const char* title, LX_core::Camera& camera);

void directionalLightPanel(const char* title,
                           LX_core::DirectionalLight& light);

} // namespace LX_infra::debug_ui
```

行为要求：

1. `renderStatsPanel(clock)`
- 显示 `frameCount()`
- 显示 `deltaTime()`（毫秒）
- 显示 `smoothedDeltaTime()` 推导出的 FPS

2. `cameraPanel(title, camera)`
- 至少显示并允许编辑：
  - `position`
  - `target`
  - `up`
  - `fovY`
  - `aspect`
  - `nearPlane`
  - `farPlane`
- helper 只负责编辑字段本身
- 是否以及何时调用 `camera.updateMatrices()` 由调用方负责，避免把隐藏副作用塞进 UI helper

3. `directionalLightPanel(title, light)`
- 基于当前 [DirectionalLight](../../src/core/scene/light.hpp) 的真实数据布局工作
- 至少覆盖：
  - `light.ubo->param.dir`
  - `light.ubo->param.color`
- 修改后 helper 应负责调用 `light.ubo->setDirty()`

注意：

- `DirectionalLight` 当前并没有高层 `direction` / `intensity` 属性；它只有 `ubo->param`
- 因此本 REQ 不应把“高层光照编辑模型”写成既成事实

### R5: 暂不把材质反射编辑器纳入本 REQ

旧版本把 `materialPanel()` 写成正式需求，但按当前代码基础，这会把本条需求一下子扩张到：

- `MaterialInstance` buffer slot 枚举
- `ShaderResourceBinding` 成员反射编辑
- 多 pass / 多 bindingName 的冲突处理
- 类型与范围 hint 规则

这些都比“薄 helper”复杂得多，而且当前仓库里并没有一个稳定的、高层的 `Material` 编辑抽象。

因此本 REQ 明确：

- 不要求在本阶段实现 `materialPanel()`
- 如果后续 demo 需要材质 UI，可先手写专用 ImGui 代码
- 材质反射面板应留给单独需求，或作为 `REQ-018` 的后续扩展子项重新定义

### R6: 测试

新增 `src/test/integration/test_debug_ui_smoke.cpp`。

测试目标以“编译/链接与极薄行为验证”为主，至少覆盖：

- 基础 helper 符号可见并可链接
- `beginPanel/endPanel`、`dragVec3`、`renderStatsPanel`、`cameraPanel`、`directionalLightPanel` 的接口存在
- 如测试环境可安全创建 ImGui context，可补一个最小 smoke case；否则允许只做链接级验证

测试注册点：

- [src/test/CMakeLists.txt](../../src/test/CMakeLists.txt)

本 REQ 不要求像素截图测试，也不要求在 headless CI 中完整验证 ImGui 绘制结果。

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/gui/debug_ui.hpp` | 新增 |
| `src/infra/gui/debug_ui.cpp` | 新增 |
| `src/infra/CMakeLists.txt` | 把 `gui/debug_ui.cpp` 加入 `INFRA_SOURCES` |
| `src/test/integration/test_debug_ui_smoke.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册测试 |

## 边界与约束

- 不引入新依赖
- 不创建新的 GUI framework、DSL 或状态系统
- helper 保持无状态；面板开关、持久显示偏好等由调用方持有
- 不要求自动材质反射编辑
- 不要求自动 scene graph 检视器
- 调用方始终可以直接混用原生 ImGui API

## 依赖

- `REQ-017`：ImGui overlay 已能在 `VulkanRenderer` 中被驱动
- `REQ-014`：`renderStatsPanel()` 依赖 `Clock::deltaTime()` / `smoothedDeltaTime()`

## 下游

- `REQ-019`：demo scene viewer 在 UI callback 中复用这些 helper
- 后续材质/后处理调试 UI：可继续沿用 `debug_ui` 的基础桥接函数与 panel 容器

## 实施状态

2026-04-17 已落地（对应 OpenSpec change `debug-ui-helper`）：

- 新增 `src/infra/gui/debug_ui.{hpp,cpp}`，命名空间 `LX_infra::debug_ui`；`.hpp` 不引入 ImGui 头，ImGui 依赖封在 `.cpp` 内
- 基础桥接齐全：`dragVec3/Vec4`、`sliderFloat/Int`、`colorEdit3/4`、`labelText`（`const char*` / `std::string` 双重载）、`labelFloat/Int`、`labelStringId`（空名回退 `"(empty #<id>)"`）；`.cpp` 顶部有 `sizeof(Vec3f)` / `sizeof(Vec4f)` 静态断言
- Panel 容器：`beginPanel/endPanel`（`FirstUseEver` 默认位置 `{8,8}`、默认大小 `{320,400}`），`beginSection/endSection`（`CollapsingHeader` + no-op pop），`separatorText`
- 组合 panel：`renderStatsPanel(Clock&)` 显示 frame count / dt (ms) / fps；`cameraPanel` 编辑 `position/target/up/fovY/aspect/near/far` 但**不**内部 `updateMatrices()`；`directionalLightPanel` 直接编辑 `ubo->param.{dir,color}`，任一 widget 变更即 `light.ubo->setDirty()`
- 不包含 `materialPanel` / scene graph inspector / DSL / 新 framework
- `src/infra/CMakeLists.txt` 的 `INFRA_SOURCES` 追加 `gui/debug_ui.cpp`
- 集成测试 `test_debug_ui_smoke` 覆盖链接级符号可见 + CPU-only ImGui smoke（CreateContext → NewFrame → helper 全套 → EndFrame → DestroyContext），无 display 即可运行
