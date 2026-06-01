# REQ-063-a: Compute Pipeline Foundation

> 2026-05-28 新增：3DGS 渲染需要 GPU 投影、排序或合成能力，通用 compute shader / compute pipeline 支持必须先落地。本 REQ 只做单 queue 串行 compute 基础设施，不要求 async compute。

## 背景

当前代码已经有一部分 compute 相关基础：

| 已有能力 | 当前事实 |
|---|---|
| Shader stage | `ShaderStage::Compute` 已存在 |
| Shader compiler | `.comp` 可映射到 `shaderc_compute_shader` |
| Reflection | `StorageBuffer` 已能从 SPIR-V 反射出来 |
| Descriptor type | Vulkan descriptor path 已识别 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` |

但 Vulkan 执行链还没有真正 compute pipeline：

| 缺口 | 当前事实 |
|---|---|
| Pipeline abstraction | `PipelineBuildDesc` 只描述 graphics pipeline |
| Pipeline creation | `VulkanPipeline::buildGraphicsPpl()` 只调用 `vkCreateGraphicsPipelines` |
| Shader module binding | `VulkanPipeline::loadShaders()` 只保存 vertex / fragment module |
| Command recording | 没有 `vkCmdDispatch` 封装 |
| FrameGraph | `FramePass` 没有 compute pass / queue kind 表达 |
| Queue | 后端只稳定暴露 graphics / present；独立 compute queue 属于后续 async compute |

因此 3DGS 的 `REQ-063-b` 不应直接承担通用 compute 基建。先做本 REQ，再做 3DGS Vulkan splat pass。

## 目标

1. 让 LXEngine 能创建并运行最小 compute shader。
2. 支持 compute descriptor layout、storage buffer 绑定和 dispatch。
3. 在 graphics queue 上串行执行 compute work，先不做独立 compute queue。
4. 为后续 3DGS GPU projection / sorting / composite 和 IBL GPU bake 等工作提供通用能力。

## 需求

### R1: Compute pipeline 描述

新增 backend-agnostic compute pipeline 描述，建议：

- `ComputePipelineDesc`
- `ComputePipelineKey`
- 或在现有 pipeline identity 中加入明确的 `PipelineKind::Compute`

该描述 SHALL 包含：

| Field | Meaning |
|---|---|
| compute shader bytecode | 单个 `ShaderStage::Compute` SPIR-V |
| reflected bindings | UBO / SSBO / storage image / sampled image 等 binding |
| push constant range | compute shader 可见 |
| local size metadata | 反射或显式配置的 workgroup size |

`PipelineBuildDesc` 当前是 graphics pipeline 合同，SHALL NOT 被含糊复用成 compute pipeline。

### R2: Shader reflection 扩展

`ShaderReflector` SHALL 支持 compute shader 的反射结果。

至少覆盖：

- `StorageBuffer`。
- `UniformBuffer`。
- sampled texture / sampler（如果 shader 声明）。
- local workgroup size（来自 SPIR-V execution mode 或显式 fallback）。

如果首版暂不支持 storage image，SHALL 在 spec 和错误信息中明确。

### R3: Vulkan compute pipeline

Vulkan backend SHALL 新增 compute pipeline 创建路径：

- 为 compute stage 创建 `VkShaderModule`。
- 从 reflected bindings 创建 descriptor set layouts。
- 创建 `VkPipelineLayout`。
- 调用 `vkCreateComputePipelines`。
- 用 RAII 生命周期释放 shader module、pipeline layout、pipeline。

该实现 SHALL 与 graphics pipeline 共享 descriptor layout helper，但 SHALL NOT 依赖 render pass、vertex layout、raster state 或 color/depth attachment。

### R4: Command buffer dispatch

Vulkan command buffer SHALL 支持：

- bind compute pipeline。
- bind compute descriptor sets。
- push compute constants（如需要）。
- `dispatch(groupCountX, groupCountY, groupCountZ)`。
- 在同一 graphics-capable queue 上串行提交 compute command buffer。

首版 SHALL 使用 graphics queue 执行 compute dispatch，因为 Vulkan graphics queue family 通常也支持 compute；独立 compute queue / async overlap 是后续扩展。

### R5: Storage buffer 资源

Core / backend SHALL 提供可被 compute shader 读写的 storage buffer path。

要求：

- buffer usage 包含 `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`。
- 支持 host upload 初始数据。
- 支持 compute 写入后被 graphics 或 CPU 验证读取的路径之一。
- descriptor routing 能按 reflected binding name 绑定 storage buffer。

### R6: Barrier 与同步

首版 SHALL 支持同 queue 内的手工 barrier：

| Flow | Required barrier |
|---|---|
| transfer upload -> compute read | transfer write -> shader read |
| compute write -> compute read | shader write -> shader read |
| compute write -> graphics read | shader write -> vertex/fragment shader read 或 vertex input read |

跨 queue ownership transfer 不属于本 REQ。

### R7: Smoke 测试

新增最小 compute smoke：

- 一个 `.comp` shader 读取 storage buffer A，写 storage buffer B。
- 测试 dispatch 后验证 B 的内容。
- Vulkan validation 无错误。
- 无独立 compute queue 的设备也能通过，因为首版走 graphics queue。

## 修改范围

- `src/core/pipeline/`
- `src/core/rhi/`
- `src/infra/shader_compiler/`
- `src/backend/vulkan/details/pipelines/`
- `src/backend/vulkan/details/commands/`
- `src/backend/vulkan/details/device_resources/`
- `assets/shaders/glsl/`
- `src/test/`
- `openspec/specs/` 新增或扩展 compute pipeline spec

## 边界与约束

- 本 REQ 不要求 async compute。
- 本 REQ 不要求独立 compute queue。
- 本 REQ 不要求 compute pass 进入完整 FrameGraph DAG。
- 本 REQ 不实现 3DGS 专用算法，只提供通用 compute pipeline 能力。

## 依赖

- `openspec/specs/cpp-style-guide/spec.md`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `notes/roadmaps/research/async-compute/01-async-compute-是什么.md`
- `notes/roadmaps/research/async-compute/04-LX当前状态对照.md`

## 后续工作

- `REQ-063-b`: 3DGS Vulkan splat pass 消费 compute foundation。
- 后续 async compute REQ：独立 compute queue、跨 queue ownership transfer、FrameGraph queue affinity、自动 barrier。

## 实施状态

Draft，未实施。
