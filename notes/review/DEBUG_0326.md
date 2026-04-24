# Vulkan 渲染器黑屏问题调试复盘

**日期**: 2026-03-26  
**症状**: `test_render_triangle` 窗口启动正常，但画面全黑，无任何几何体显示  
**环境**: Windows 10, NVIDIA GeForce RTX 3070 Ti Laptop GPU, Vulkan 1.3.224

---

## 一、最终定位的 Bug

### Bug 1: `colorBlendAttachment` 悬空指针（根因）

**文件**: `src/backend/vulkan/details/pipelines/pipeline.cpp`

```cpp
// ❌ 修复前
VkPipelineColorBlendStateCreateInfo
VulkanPipelineBase::getColorBlendStateCreateInfo() {
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};  // 局部变量
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | ...;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.pAttachments = &colorBlendAttachment;  // 指向局部变量
  return colorBlending;  // 返回后 colorBlendAttachment 析构，pAttachments 悬空
}
```

**影响**: `vkCreateGraphicsPipelines` 读取 `pAttachments` 时，`colorWriteMask` 值为栈上残留数据（大概率为 0）。`colorWriteMask = 0` 意味着片段着色器的输出**无法写入任何颜色通道**，因此只能看到清除色。

**修复**: 将 `colorBlendAttachment` 提升为类成员变量 `m_colorBlendAttachment`（与 `m_viewport`、`m_scissor` 同一模式）。

### Bug 2: `createLayout()` 中 Push Constant Range 悬空指针

**文件**: `src/backend/vulkan/details/pipelines/pipeline.cpp`

```cpp
// ❌ 修复前
if (m_pushConstants.size > 0) {
    VkPushConstantRange range{};       // if 块内的局部变量
    range.stageFlags = ...;
    pipelineLayoutInfo.pPushConstantRanges = &range;
}  // range 在此析构
// vkCreatePipelineLayout 在此调用 — pPushConstantRanges 已悬空
vkCreatePipelineLayout(...);
```

**修复**: 将 `range` 声明移到 `if` 块外部，保证其生命周期覆盖 `vkCreatePipelineLayout` 调用。

### Bug 3: `perspective()` 矩阵初始化错误

**文件**: `src/core/math/mat.hpp`

`Mat4T` 默认构造为单位矩阵（`m[3][3] = 1`）。`perspective()` 函数使用默认构造后再赋值各分量，但遗漏了将 `m[3][3]` 置零。Vulkan 透视矩阵要求 `m[3][3] = 0`（用于透视除法），残留的 `1` 导致 `w` 分量计算错误。

**修复**: 先将矩阵全部显式清零，再设置透视矩阵各分量。

### Bug 4: `fovY` 单位未转换

**文件**: `src/core/scene/camera.hpp`

`Camera::fovY` 注释为"单位：度"，默认值 45.0f。但 `Mat4f::perspective()` 的参数 `fovYRad` 期望弧度。直接传入 45.0f 导致 `tan(45/2) = tan(22.5)` 变成 `tan(22.5 弧度)` ≈ 极大值，投影矩阵严重失真。

**修复**: 在 `updateMatrices()` 中添加 `fovY * (π / 180)` 转换。

### Bug 5: 测试三角形顶点过大

**文件**: `src/test/test_render_triangle.cpp`

三角形顶点坐标 `±5.0f`，相对于相机位置 `z=3.0f` 过大，即使矩阵正确，NDC 坐标也超出 `[-1, 1]`，被完全裁剪。

**修复**: 缩小至 `±1.0f`。

---

## 二、调试过程与手段

### 阶段 1: 初步排查（环境变量开关）

添加了一组环境变量控制的调试开关：

| 环境变量 | 功能 |
|---------|------|
| `LX_RENDER_DEBUG=1` | 启用渲染器日志（acquire/present 结果、资源数量等） |
| `LX_RENDER_DEBUG_CLEAR=1` | 清除色改为蓝色 |
| `LX_RENDER_DISABLE_CULL=1` | 关闭背面剔除 |
| `LX_RENDER_DISABLE_DEPTH=1` | 关闭深度测试 |
| `LX_RENDER_FLIP_VIEWPORT_Y=1` | 翻转视口 Y 轴 |

**收获**: 蓝色清除色生效证明了 swapchain → render pass → framebuffer → present 链路正常。问题锁定在**绘制命令未产生可见输出**。

### 阶段 2: CPU 侧矩阵验证

在测试程序中添加了逐顶点的 CPU 侧变换日志：

```
vertex0 view=(-1,1,-3,1), clip=(-1.81,2.41,2.90,3), ndc=(-0.60,0.80,0.97)
```

通过比对 NDC 坐标与 `[-1, 1]` 范围，发现了透视矩阵和 fovY 的错误（Bug 3、4），以及顶点过大的问题（Bug 5）。

### 阶段 3: 最小化 Shader 隔离

修复数学问题后仍然黑屏。采用**彻底精简 shader**策略：

```glsl
// 顶点着色器：零依赖，硬编码三角形
void main() {
    vec2 positions[3] = vec2[]( vec2(-0.5,-0.5), vec2(0.5,-0.5), vec2(0.0,0.5) );
    gl_Position = vec4(positions[gl_VertexIndex % 3], 0.0, 1.0);
}

// 片段着色器：零依赖，纯红色输出
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0, 0.0, 0.0, 1.0); }
```

配合 `draw()` 函数中的 `if(true)` 旁路（跳过所有资源绑定，直接 `vkCmdDraw(3)`），最终定位到管线状态本身的问题。

### 阶段 4: 管线状态强制覆盖 + 日志

- 强制 `cullMode = VK_CULL_MODE_NONE`
- 强制 `depthTestEnable = VK_FALSE`
- 强制蓝色背景（便于区分清除色与渲染输出）
- 在 `buildGraphicsPpl()` 中 dump 全部管线状态

```
colorWriteMask=0xf, blendEnable=0
pAttachments=00000154CC0D29E8 (&member=00000154CC0D29E8)  ← 地址一致，确认修复生效
```

**这一步三角形首次出现**，确认了 `colorBlendAttachment` 悬空指针是根因。

### 阶段 5: 二分法逐步恢复

按以下顺序逐一恢复原始设置，每步验证三角形是否消失：

| 步骤 | 恢复内容 | 结果 |
|------|---------|------|
| 1 | 恢复 `depthTestEnable = VK_TRUE` | ✅ 三角形正常 |
| 2 | 恢复 `cullMode = VK_CULL_MODE_BACK_BIT` | ❌ 三角形消失* |
| 3 | 恢复正常渲染路径（bindResources + drawIndexed） | ✅ 三角形正常 |
| 4 | 恢复完整原始 shader（UBO 声明 + Blinn-Phong 光照） | ✅ 三角形正常 |
| 5 | 移除所有调试代码 | ✅ 正常渲染 |

*Step 2 的剔除问题仅影响调试用硬编码三角形（Vulkan Y-down 坐标系下为 CW 绕序），不影响实际测试三角形（经 view+proj 变换后为 CCW 绕序）。

---

## 三、关键教训

### 1. Vulkan 中返回结构体时注意指针成员的生命周期

Vulkan 的 `CreateInfo` 结构体大量使用 `pXxx` 指针成员。如果将创建逻辑拆分到子函数中并按值返回 `CreateInfo`，必须确保所有被指向的数据**生命周期覆盖 `vkCreateXxx` 的调用点**。

**推荐模式**: 将被指向的数据存为类成员变量（如 `m_viewport`、`m_scissor`、`m_colorBlendAttachment`），而非在子函数中使用局部变量。

### 2. 清除色测试是最简单的链路验证

设置一个非黑色的清除色（如蓝色）可以快速验证 swapchain → render pass → framebuffer → present 链路是否工作。如果清除色正确显示，问题就在绘制命令或管线状态上。

### 3. 最小化 Shader 是强大的隔离工具

当怀疑问题在 CPU 数据 vs 管线状态时，用零依赖的硬编码 shader + `vkCmdDraw` 可以完全绕过资源系统，快速验证管线本身是否正常。

### 4. 二分法恢复比猜测高效

确认最小可工作状态后，逐步恢复原始代码并验证每一步，比同时修改多处然后猜测哪个生效要可靠得多。

### 5. Vulkan 的绕序约定

Vulkan framebuffer 坐标系 Y 轴朝下。Vulkan spec 的面积公式带有负号：`a = -(1/2) * Σ(...)`。在不翻转 Y 轴（viewport 或 projection）的情况下，标准数学坐标系中的 CCW 三角形在 Vulkan framebuffer 中变为 CW。设置 `frontFace` 时需考虑投影矩阵的 Y 约定。

## 补充最小化shader
vertex shader
```glsl
#version 450

// DEBUG: absolute minimum vertex shader - hardcoded triangle, zero dependencies
void main() {
    vec2 positions[3] = vec2[](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.0,  0.5)
    );
    gl_Position = vec4(positions[gl_VertexIndex % 3], 0.0, 1.0);
}
```

fragment shader
```glsl
#version 450

// DEBUG: absolute minimum fragment shader - just output solid red
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
```

对应的draw代码
```cpp
vkCmdBindPipeline(rawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.getHandle());

VkViewport viewport{};
viewport.x = 0.0f;
viewport.y = 0.0f;
viewport.width = static_cast<float>(extent.width);
viewport.height = static_cast<float>(extent.height);
viewport.minDepth = 0.0f;
viewport.maxDepth = 1.0f;
vkCmdSetViewport(rawCmd, 0, 1, &viewport);

VkRect2D scissor{};
scissor.offset = {0, 0};
scissor.extent = extent;
vkCmdSetScissor(rawCmd, 0, 1, &scissor);

vkCmdDraw(rawCmd, 3, 1, 0, 0);
```

