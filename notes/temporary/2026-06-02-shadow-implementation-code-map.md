# 2026-06-02 阴影实现代码地图

阴影实现可以想成一条“从灯光拍深度底片，再在主相机里查底片”的流水线。我们先用 `Shadow` pass 从方向光视角写出 depth，再让 `Forward` pass 在片元 shader 中采样 `ShadowMap0..3`，判断当前像素是否被遮挡。

这份临时笔记只记录当前代码在哪里、各层负责什么，方便后续重构前快速定位。

## 整条链路先分成五层

| 层级 | 负责什么 | 关键代码 |
|---|---|---|
| Shader 合同 | depth-only 写入、forward 阴影采样、cascade 选择、bias 和 debug mode | `assets/shaders/glsl/shadow_depth_only.*`、`assets/shaders/glsl/blinnphong_0.*` |
| Light / CSM 数据 | 方向光参数、cascade 矩阵、split、shadow map size、bias、strength | `src/core/scene/light.hpp`、`src/core/scene/light.cpp` |
| FrameGraph / RenderQueue | 建立 `Pass_Shadow` 队列，声明 `shadow.cascade*` 写入和 `ShadowMap*` 读取 | `src/core/frame_graph/*` |
| Vulkan backend | 创建 depth attachment，执行 shadow pass，transition 后绑定 sampled image | `src/backend/vulkan/*` |
| Editor / Scene 参数 | 从 scene YAML、Inspector、CommandBus 写入 shadowStrength / distance / cascade count | `src/demos/lxe_editor/*`、`src/infra/scene_io/*` |

## Shader：底片怎么写，Forward 怎么查

Shadow pass 的 shader 很薄，只关心 light clip space。Forward shader 才包含主要阴影判断逻辑。

| 文件 | 当前职责 | 重点阅读点 |
|---|---|---|
| `assets/shaders/glsl/shadow_depth_only.vert` | 使用 `sceneLight.shadowViewProj * model * position` 输出 depth-only pass 的 `gl_Position` | `LightUBO` layout、`gl_Position = sceneLight.shadowViewProj * worldPos` |
| `assets/shaders/glsl/shadow_depth_only.frag` | 空 fragment，依赖 depth attachment 写入 | depth-only pass 不输出颜色 |
| `assets/shaders/glsl/blinnphong_0.vert` | Forward pass 顶点输出 world position / normal，并声明同一套 `LightUBO` | UBO layout 要和 fragment / C++ 一致 |
| `assets/shaders/glsl/blinnphong_0.frag` | 采样 `ShadowMap0..3`，选择 cascade，执行 PCF 和 bias，输出 debug shadow mode | `selectCascade`、`sampleShadowCascade`、`sampleShadowMap`、`debugShadowMode` |

`blinnphong_0.frag` 是观察阴影 bug 时最集中的 shader 入口：

| 函数 / 字段 | 当前含义 |
|---|---|
| `ShadowMap0..3` | Forward pass 读取的四张 cascade depth texture |
| `selectCascade(viewDepth)` | 根据 view-space depth 选择 cascade |
| `isInsideShadowDistance(viewDepth)` | 超出最后 cascade split 时返回无阴影 |
| `sampleShadowCascade(...)` | 把 world position 投到 cascade light space，并做 PCF |
| `sceneLight.cascadeDepthRanges` | 把 world-space bias 转成 normalized depth bias |
| `material.debugShadowMode` | 调试输出 shadow visibility 或 cascade color |

## DirectionalLight：CSM 的 CPU 合同

方向光的 shadow 数据保存在 `DirectionalLightData` 里。它既服务 shadow pass，也服务 forward pass。

| 字段 | 所在文件 | 当前含义 |
|---|---|---|
| `shadowViewProj` | `src/core/scene/light.hpp` | 当前正在录制的 shadow cascade 使用的矩阵 |
| `cascadeViewProj[MaxShadowCascades]` | `src/core/scene/light.hpp` | Forward shader 采样四个 cascade 时使用的矩阵数组 |
| `cascadeSplits` | `src/core/scene/light.hpp` | 按主相机 view depth 切出来的 cascade 终点 |
| `cascadeDepthRanges` | `src/core/scene/light.hpp` | 每个 cascade 的 light depth 范围，用来归一化 bias |
| `shadowParams.x` | `src/core/scene/light.hpp` | shadow map size |
| `shadowParams.y` | `src/core/scene/light.hpp` | world-space shadow bias |
| `shadowParams.z` | `src/core/scene/light.hpp` | shadow strength |
| `shadowParams.w` | `src/core/scene/light.hpp` | cascade count |

核心函数都在 `src/core/scene/light.cpp`：

| 函数 | 当前职责 |
|---|---|
| `DirectionalLight::updateShadowCascadesForCamera` | 根据 active camera 和 light direction 计算 split、cascade bounds、light view/proj |
| `DirectionalLight::makeShadowCascadeUBOSnapshot` | 为某个 cascade 生成只含当前 `shadowViewProj` 的 UBO snapshot |
| `DirectionalLight::setActiveShadowCascade` | 把 `cascadeViewProj[i]` 复制到 `shadowViewProj`，供 shadow pass 使用 |
| `DirectionalLight::updateShadowViewProjection` | 没有 active camera cascade 时的基础 shadow matrix 初始化 |
| `setShadowBias / setShadowStrength / setShadowDistance / setShadowCascadeCount` | Inspector / CommandBus / scene load 写入 shadow 参数 |

这里是矩阵 bug、coverage bug、bias bug 最容易交织的地方。我们排查时通常先确认 CPU 侧 `cascadeViewProj[i]` 是否跟 active camera 变化一致，再看 shader 采样边界。

## FrameGraph：四张 depth resource 怎样串到 Forward

FrameGraph 把 shadow 当成显式 pass，而不是 renderer 内部的隐藏步骤。当前方向光 cascade 会被展开成多个 `Pass_Shadow`。

| 文件 | 当前职责 |
|---|---|
| `src/core/frame_graph/pass.hpp` | 定义 `Pass_Shadow` |
| `src/core/frame_graph/frame_graph.hpp` | 定义 `FramePass`、`FrameGraphRead`、`FrameGraphWrite`、`FrameGraphSampledResource` |
| `src/core/frame_graph/frame_graph.cpp` | 构建和编译 pass / resource read-write 合同 |
| `src/core/frame_graph/render_queue.cpp` | 为 `Pass_Shadow` 构建 caster queue；无 target camera 时 fallback 到 `VisibilityMask_All` |

CSM resource 命名关系如下：

| Cascade | Shadow pass 写出 | Forward shader binding |
|---|---|---|
| 0 | `shadow.cascade0` | `ShadowMap0` |
| 1 | `shadow.cascade1` | `ShadowMap1` |
| 2 | `shadow.cascade2` | `ShadowMap2` |
| 3 | `shadow.cascade3` | `ShadowMap3` |

如果未来重构 shadow 架构，我们需要保护这条合同：FrameGraph 的 resource name 和 shader binding name 必须一一对应，否则 descriptor 绑定会失配。

## Vulkan backend：depth attachment 和 descriptor 在这里落地

Vulkan 层负责把 FrameGraph 的抽象资源变成真实 image / framebuffer / descriptor。

| 文件 | 当前职责 | 阴影相关入口 |
|---|---|---|
| `src/backend/vulkan/vulkan_renderer_foundation.cpp` | 构建和维护 frame graph，组织 shadow cascade pass 与 forward pass | 搜 `shadow.cascade`、`Pass_Shadow`、`ShadowMap` |
| `src/backend/vulkan/vulkan_renderer.cpp` | 执行 frame graph pass、draw queue、调度渲染流程 | 搜 `Pass_Shadow`、frame graph pass execution |
| `src/backend/vulkan/details/resource_manager.cpp` | 创建 / 查找 / 更新 frame graph attachment | `createOrGetFrameGraphAttachment`、`getFrameGraphAttachment`、`updateFrameGraphAttachmentLayout` |
| `src/backend/vulkan/details/commands/command_buffer.cpp` | 根据 shader reflection 和 `RenderingItem` 绑定 descriptor | `VulkanCommandBuffer::bindResources` 中处理 `FrameGraphSampledResource` |

`bindResources` 是 Forward shader 真正拿到 `ShadowMap0..3` 的位置：它看到 `FrameGraphSampledResource("shadow.cascade2", "ShadowMap2")` 后，会向 resource manager 查当前 frame 的 attachment，再写入 combined image sampler descriptor。

## Scene 和 Editor：参数从哪里来

阴影参数不是只在 renderer 内部硬编码。scene YAML、editor runtime、Inspector / CommandBus 都会参与写入。

| 文件 | 当前职责 |
|---|---|
| `src/infra/scene_io/scene_document.hpp` | `LightState` 中保存 `shadowStrength`、`shadowDistance`、`shadowCascadeCount` 默认值 |
| `src/infra/scene_io/scene_document.cpp` | 从 YAML 读取 / 写出 shadow 参数 |
| `src/demos/lxe_editor/scene_runtime.cpp` | 把 `LightState` 应用到 `DirectionalLight`，保存 runtime scene 时读回 shadow 参数 |
| `src/demos/lxe_editor/scene_builder.cpp` | 对一些内置 patch / plane 类对象禁用 `Pass_Shadow`，避免单面接收面参与 cast shadow |
| `src/core/scene/scene.cpp` | `getSceneLevelResources` 把 light UBO 和 `SceneLightsUBO` 加入 pass descriptor resources |

典型 YAML 字段如下：

```yaml
light:
  type: Directional              # -> DirectionalLight
  shadowStrength: 0.7            # -> DirectionalLight::setShadowStrength
  shadowDistance: 80.0           # -> DirectionalLight::setShadowDistance
  shadowCascadeCount: 4          # -> DirectionalLight::setShadowCascadeCount
```

## 现有验证入口

| 测试 / 文档 | 当前覆盖 |
|---|---|
| `src/test/integration/test_vulkan_frame_graph.cpp` | frame graph 中 shadow cascade pass 数量、资源读写、active camera 更新 cascade matrix |
| `src/test/integration/test_scene_runtime.cpp` | shadow tutorial scene load/save、patch 禁用 shadow pass、debugShadowMode |
| `src/test/integration/test_scene_document.cpp` | shadow YAML 字段 round trip |
| `notes/concepts-design/rendering-pipeline/shadow-pass.md` | shadow pass 的概念解释 |
| `notes/concepts-design/rendering-pipeline/cascaded-shadow-maps.md` | CSM 数据流解释 |
| `notes/subsystems/vulkan-backend.md` | 之前 shadow matrix / coverage / bias 调试记录 |

## 后续重构时先盯住的边界

| 边界 | 为什么重要 |
|---|---|
| `DirectionalLightData` C++ layout 与 GLSL `LightUBO` layout | 任一字段偏移不一致都会让 shadow matrix / split / bias 出错 |
| `shadow.cascade*` resource name 与 `ShadowMap*` binding name | FrameGraph 和 descriptor 绑定靠这个名字桥接 |
| `shadowViewProj` 与 `cascadeViewProj[i]` 的关系 | Shadow pass 写 depth 用前者，Forward pass 采样用后者 |
| active camera 更新 cascade 的时机 | 相机移动后 shadow coverage 是否同步取决于这条链路 |
| bias 的单位 | 当前 shader 把 world bias 除以 cascade depth range，重构时不能混回固定 normalized bias |
| 单面 plane / patch 的 shadow pass enable 状态 | 接收面和投影体的语义要分开，否则容易出现自阴影和穿插问题 |

## 我们已经记录了什么

我们把当前阴影实现拆成了 shader、light/CSM CPU 数据、FrameGraph、Vulkan backend、Scene/Editor 参数五层，并标出了每层最值得读的文件和函数。后续要重构时，可以先沿着 `DirectionalLightData -> FrameGraph shadow.cascade* -> Vulkan descriptor -> blinnphong_0.frag` 这条链路检查合同是否一致。

## 下一步

如果要进入重构设计，我们可以先写一个新的需求文档，把 bug 分成矩阵一致性、coverage 策略、bias 策略、resource binding 校验四类，再决定哪些属于 shader 修复，哪些属于 FrameGraph / renderer 结构调整。
