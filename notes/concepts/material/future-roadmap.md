# 材质系统未来路线：Bindless、Variants 与 FrameGraph

当前材质系统像一套按步骤出菜的厨房：`MaterialTemplate` 决定每一步用什么工具，`MaterialInstance` 提供这道菜的参数和纹理，`RenderWorkQueue` 把每一步要执行的 pipeline work 排成列表。未来路线不是推倒这套模型，而是把资源绑定、shader 变量策略和 pass 依赖逐步变得更数据驱动。

> 状态：本页描述 roadmap 设计方向，**尚未实施**。当前可执行行为仍以 `src/`、当前设计 spec 和当前概念页为准。

## 先纠正 bindless 与 PSO 数量的关系

Bindless 经常被理解成“上了以后 pipeline 数量自然下降”。roadmap 里的结论更细：

| 机制 | 主要减少什么 | 不能自动减少什么 |
|---|---|---|
| Bindless descriptor | descriptor set layout 数量、pipeline layout 数量、CPU 绑定频率 | shader 代码差异、render state 差异、pass 差异 |
| Ubershader + uniform branch | 材质特性 permutation 数量 | vertex layout、blend/depth/MSAA/RT format 等硬件 state 差异 |
| GPU-driven / vertex pulling | CPU draw 组织成本、部分 vertex input 差异 | pass 输出契约差异 |

所以更准确的说法是：**bindless 让资源绑定接口收敛；PSO 数量下降需要同时调整 shader 变量策略和 draw 组织方式。**

## 当前模型已经保留的未来接口

| 当前边界 | 当前用途 | 未来价值 |
|---|---|---|
| `MaterialTemplate` / `MaterialInstance` 分层 | template 管结构，instance 管运行时值 | template 可继续承载 shader ABI；instance 可改成保存 handle/slot/index |
| `ShaderProgramSet.variants` 属于 pass | variants 进入 pipeline signature | 未来可把部分材质特性 variant 下沉为 uniform branch |
| binding ownership 名字表 | 区分 system-owned 与 material-owned | 可扩展成固定 shader ABI 校验表 |
| `PipelineKey` 结构化 compose | object/material signature 组成 key | 便于保留真正会切 PSO 的维度，移除 layout 噪声 |
| `getDescriptorResources(pass)` pass-aware | 每个 pass 只取自己使用的 material-owned binding | 可演进为记录 pass 资源 handle/slot/依赖 |

这也是为什么当前不把 `.material resources` 写成“shader 可见资源总表”。它只描述材质自己拥有的纹理默认值。`CameraUBO`、`LightUBO`、`SceneLightsUBO`、`Bones` 属于系统 ABI，未来也更可能进入全局 ABI 或 bindless buffer/descriptor 体系。

## Variants 未来会分层收敛

当前 variants 是 template/pass 级结构，启用组合会进入 `ShaderProgramSet::getPipelineSignature()`，因此会影响 `PipelineKey`。这适合现在的传统 pipeline 路径。

未来 roadmap 倾向把差异分层：

| 差异类型 | 当前做法 | 未来可能做法 | 状态 |
|---|---|---|---|
| Shadow / GBuffer / Forward 这类 pass 契约差异 | 不同 pass / shader | 仍然分 pass、分 shader | 未实施，方向明确 |
| cull/depth/blend/MSAA/RT format | `RenderState` / target 进入 pipeline 输入 | 仍然切 pipeline | 未实施，方向明确 |
| normal map、IBL、detail layer 等材质特性 | variants 生成 permutation | ubershader + uniform branch，少量热点保留 specialization constant | 未实施 |
| 纹理和参数差异 | descriptor resources + UBO bytes | bindless slot / material index / buffer offset | 未实施 |

这里最重要的结论是：**不是所有 variants 都应该消失**。硬件 state、pass 契约、shader stage 输入输出差异仍会保留为 pipeline 维度；适合减少的是材质特性组合造成的 permutation。

## Reflection 在 bindless 后仍然重要

传统路径里，reflection 常用于从 shader 自动生成 descriptor set layout。bindless 路线中，roadmap 方向更像这样：

```text
引擎先定义少量固定 ABI / layout
  -> shader reflection 校验 shader 是否符合 ABI
  -> 材质系统记录 shader 使用了哪些逻辑资源
  -> MaterialInstance 提供对应 handle / slot / buffer offset
```

所以 reflection 不会消失，它的职责会从“生成 layout”转成“校验 ABI + 记录资源需求”。这和当前 `MaterialTemplate::rebuildMaterialInterface()` 是连续的：我们已经在 template 层收束 shader 反射出的 material-owned binding，只是当前输出仍然是传统 descriptor resource 列表。

## FrameGraph 未来才会接管依赖与同步

当前 `FrameGraph` 只按显式 `FramePass` 顺序构建 queue，不分析资源依赖。roadmap 里的完整路径会分阶段引入：

| 阶段 | 目标 | 与材质/资源的关系 | 状态 |
|---|---|---|---|
| Resource 数据结构 | pass 声明输入/输出资源 | shader/material 资源依赖有地方登记 | 候选 REQ，未实施 |
| compile: DAG + 拓扑 | pass 顺序由资源边推导 | 被依赖的 pass 自动先执行 | 候选 REQ，未实施 |
| 自动 renderpass/framebuffer | backend attachment 创建收敛 | pass target 从手写走向 graph compile | 候选 REQ，未实施 |
| barrier 推导 | 根据资源读写自动插 barrier | 跨 pass 资源可以安全同步 | 候选 REQ，未实施 |
| 多 queue / timeline | graphics/compute/transfer 协同 | 上传、compute 生成数据、draw 消费之间形成 timeline 依赖 | 工业级延伸，未实施 |

这里需要和当前实现分清：今天的 `FrameGraph` 不建立资源边，也不生成 semaphore。timeline semaphore 的 roadmap 设计是“queue / submit 进度线 + resource retire value”，不是“每个 texture 或 UBO 自己绑定一个 semaphore”。

## 对 .material resources 的长期解释

当前：

```yaml
resources:
  albedoMap: white      # -> material-owned Texture2D / TextureCube 默认值
```

未来 bindless 后，这个字段大概率仍有价值，但含义会从“绑定一个 descriptor resource”变成“给材质逻辑资源一个默认 asset/handle”。它不会成为 system-owned UBO/SSBO 的声明处。

| 字段 | 当前含义 | 未来可能含义 |
|---|---|---|
| `parameters` | 写入 canonical `ParameterBuffer` 的默认值 | 写入 material parameter buffer / SSBO / packed blob |
| `resources` | 给 material-owned texture binding 绑定默认纹理 | 给逻辑 texture slot 分配默认 asset handle |
| system-owned names | 不写进 `.material`，由系统注入 | 由固定 ABI、全局 buffer、bindless table 或 FrameGraph resource 提供 |

因此“是否保留 `resources` 字段”的答案是：**保留，但收窄含义**。它描述材质资产自己的默认资源，而不是系统注入资源或跨 pass graph 资源。

## 当前相关要求

| Requirement | 与本页关系 | 状态 |
|---|---|---|
| [REQ-044-c Editor 资产注册表与热重载桥](../../requirements/pending/044-c-editor-asset-registry-and-hot-reload-bridge.md) | 未来 material/texture/shader asset registry 和热重载入口 | 进行中，未实施 |
| FrameGraph roadmap research | 自动 DAG、barrier、aliasing 的设计来源 | 研究文档，未实施 |
| Bindless texture roadmap research | bindless descriptor、ubershader、slot 生命周期的设计来源 | 研究文档，未实施 |
| Multi-threading timeline research | timeline semaphore / resource retire value 的同步模型 | 研究文档，未实施 |

## 继续阅读

- [Bindless Texture 技术调研](../../roadmaps/research/bindless-texture/README.md)
- [Bindless: Pipeline 与 Shader 变量策略](../../roadmaps/research/bindless-texture/04-Pipeline与Shader策略.md)
- [Frame Graph 技术调研](../../roadmaps/research/frame-graph/README.md)
- [Timeline 与资源退休模型](../../roadmaps/research/multi-threading/08-Timeline与资源退休模型.md)
