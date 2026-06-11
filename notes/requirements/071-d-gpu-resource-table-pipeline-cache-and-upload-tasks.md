# REQ-071-d: GPUResourceTable、Pipeline Cache 与异步 Upload Task

> 2026-06-10 新增：本 REQ 是 `REQ-071` 连续需求族的第四步。目标是在 `REQ-071-c` 的 CPU SceneResourceTable 资源图之上，引入平台无关的 GPUResourceTable 接口、backend pipeline cache 管理和简单 task/job upload 系统，降低场景加载和 technique 切换时的卡顿。

## 背景

当前 CPU scene resource 和 GPU backend resource 的职责边界还不够清晰：

| 问题 | 影响 |
|---|---|
| Vulkan backend 自己管理大量 GPU 句柄和 pipeline cache | 难以从 core 层表达“哪些资源需要上传、上传进度如何、cache 如何复用” |
| pipeline cache 与 scene loading 缺少统一协调 | 大场景加载或切 technique 时可能大量同步编译 PSO |
| 场景加载过程缺少任务化 upload | editor 可能继续渲染半加载场景或表现为卡死 |
| scene package 未来需要关联 backend cache 信息 | 当前没有平台无关接口表达 cache blob / pipeline rebuild |

本 REQ 不追求完整多线程 job system，只引入足够支撑 scene load/upload/pipeline preload 的简单 task 模型。

## 目标

1. 在 core/RHI 层定义平台无关 `IGpuResourceTable` 接口。
2. Vulkan backend 实现 GPU resource table，管理 buffer/image/sampler/descriptor/pipeline 句柄。
3. pipeline cache 由 GPU resource table 或其子模块统一负责。
4. scene load / technique switch 可以生成 upload tasks 和 pipeline build tasks。
5. editor 在耗时加载/切换期间停止当前 scene 渲染，显示进度/log 窗口。
6. 支持导出/导入 backend cache blob，为 `REQ-071-e` scene package 做准备。
7. 本轮直接支持 bindless descriptor table 与 indirect draw，shader 通过 object/material/instance id 读取全局数组。
8. bindless descriptor table 按资源类型全局分表；SceneResourceTable 的 URI/handle/typed array snapshot 是 GPU slot 分配的唯一输入。

## 需求

### R1: Core RHI GPUResourceTable 接口

在 core/RHI 层新增平台无关接口。

最低能力：

| 能力 | 说明 |
|---|---|
| create/update buffer | vertex/index/material/object/camera/light 等 GPU buffer |
| create/update image | texture/HDR/cubemap/render-independent image |
| create sampler | sampler state |
| create descriptor table | bindless/传统 descriptor table 抽象 |
| create/update bindless table | texture、buffer、sampler 等 bindless slot 管理 |
| create/update indirect draw buffer | draw command、draw data、object/material indirection |
| getOrCreate pipeline | graphics/compute pipeline cache |
| export cache blob | backend cache 序列化 |
| import cache blob | backend cache 恢复 |
| query progress | upload/pipeline task 进度 |

core 层只定义接口和 handle，不暴露 Vulkan 类型。

### R2: Vulkan GPUResourceTable 实现

Vulkan backend SHALL 实现 `IGpuResourceTable`。

它 SHALL 管理：

- VkBuffer / allocation。
- VkImage / image view / sampler。
- descriptor set/layout 或 bindless descriptor table。
- indirect draw command buffer 与 draw data buffer。
- graphics pipeline。
- compute pipeline。
- Vulkan pipeline cache object / serialized blob。

现有 `VulkanResourceManager` / `PipelineCache` 可以被拆分、迁移或作为实现细节复用，但对上层暴露统一 GPU resource table。

### R3: Pipeline Cache 统一入口

pipeline cache SHALL 基于 `PipelineBuildDesc`、technique/pass shader reflection、render target signature 构建 key。

要求：

- `find` 不创建。
- `getOrCreate` miss 可观测。
- preload 不打印 miss warning。
- graphics/compute pipeline 统一入口，但保留类型安全。
- cache blob 可导出到 scene package metadata。
- cache blob 恢复失败时打印 warning 并重新构建，不静默忽略。

### R4: Upload Task / Job 系统

新增简单 task/job 系统，用于封装上传和 pipeline preload。

最低结构：

```text
Task {
  name
  phase
  progress
  dependencies
  run()
  diagnostics
}
```

首版任务类型：

- CPU package/resource graph load。
- mesh buffer upload。
- texture upload。
- material/object/camera/light buffer upload。
- shader compile/reflection。
- pipeline preload。
- backend cache import/export。

任务可以先用单 worker 或后台线程池实现；关键是把耗时动作拆成可报告进度的单元。

### R5: Scene Load / Technique Switch Progress UI

editor 在场景加载、scene package restore、active technique switch 等耗时操作时 SHALL：

- 暂停当前 scene 渲染或切到 loading state。
- 弹出 modal / overlay 窗口。
- 打印 task log。
- 显示总体进度和当前 task。
- 操作完成后再恢复渲染。
- 失败时显示 fatal/error diagnostics，不继续渲染半初始化场景。

### R6: Backend Cache 与 Scene Package 连接点

GPUResourceTable SHALL 提供给 `REQ-071-e` 使用的 cache metadata：

- backend name / version。
- GPU/driver/cache compatibility key。
- pipeline cache blob。
- shader/pipeline key list。
- resource upload manifest。

scene package 可以保存这些 metadata；加载时先恢复 CPU resource table，再尝试导入 backend cache，再执行 upload tasks。

### R7: 与 FrameGraph / Technique 的连接

FrameGraph compile 后 SHALL 能生成需要的 pipeline preload descs。

流程：

```text
SceneResourceTable snapshot
  -> active technique validation
  -> FrameGraph compile
  -> pipeline build desc collection
  -> GPUResourceTable preload tasks
  -> upload tasks
```

如果 technique/pass 当前 pipeline 系统不支持，validation 阶段报错，不进入 GPU upload。

### R8: Bindless Scene Data Upload

GPUResourceTable SHALL 上传 `SceneResourceTable` 导出的全局数组：

- positions。
- indices。
- geometry attribute streams。
- mesh/geometry descriptors。
- object records。
- material instance records / reflected parameter storage。
- camera/frame records。
- light records。
- texture/sampler bindless table。

shader SHALL 通过 object/material/instance id 读取这些数组，不再依赖每个 draw 绑定一套 material descriptor。

要求：

- material parameters 由 reflection + MaterialInstance envelope 表打包到 material instance storage。
- material texture/spectrum/bsdfTable 等资源以 bindless slot 或 handle 进入 material storage。
- fixed system ABI 使用 `REQ-071-b` 的 SSBO common contract。
- 缺少 backend descriptor indexing / bindless 支持时，validation 失败并打印明确错误；不静默回退到传统 per-material descriptor。

### R8.1: Global Bindless Tables By Resource Kind

bindless table SHALL 按资源类型全局组织，而不是按 material、object、template 或 pass 局部分配。

最低表结构：

| 表 | 内容 | 典型引用方 |
|---|---|---|
| texture table | 2D/3D/cubemap/HDR image view slot | material record、effect record、environment |
| sampler table | sampler state slot | material record、effect record |
| buffer table | geometry attribute stream、spectrum、bsdf table、custom buffer slot | mesh/material/effect record |
| material storage | 按 `MaterialTemplate` / `bsdf.type` 分组的 reflected material parameter records | object/draw record |
| object storage | transform、mesh index、material index、visibility/render flags | draw data |
| mesh/geometry storage | position/index/attribute ranges and stream indices | draw data / shader |
| camera/light storage | fixed system ABI arrays | frame/effect/material shader |

规则：

- CPU `SceneResourceTable` 的 canonical URI + resource handle 决定资源唯一性；GPUResourceTable 只把 table snapshot 中的资源 handle 映射到 bindless slot。
- GPUResourceTable SHALL 维护 `ResourceHandle -> GpuSlot/GpuHandle` 映射，避免相同 CPU resource 重复上传或重复分配 slot。
- `MaterialInstance` GPU record 只保存 texture/sampler/buffer/material parameter storage 的索引或 handle，不保存路径字符串，也不持有 backend 对象指针。
- `SceneObject` / draw data 只保存 object index、mesh/geometry index、material index 和必要的 instance range。
- bindless slot 可以在一次 scene load snapshot 内稳定；跨 scene package restore 的稳定性由 `REQ-071-e` 的 package metadata/cache 处理，不要求裸 slot id 成为长期文件格式身份。
- 不允许按材质或按 pass 临时创建局部 descriptor set 来绕过全局 bindless table。

### R8.2: Material Parameter Storage By Template

material parameter storage SHALL 按 `MaterialTemplate` / `bsdf.type` 分组，而不是把所有 BSDF 的参数塞入一个超大 variant record。

规则：

- 每个 `MaterialTemplate` 在某个 technique/pass 下有自己的 reflected parameter layout 和 storage buffer。
- 同一 `MaterialTemplate` 的多个 `MaterialInstance` 以数组元素形式写入该 template storage。
- object/draw record 保存 material template id 与该 template storage 内的 material instance index，或保存能等价定位到二者的 material handle。
- pipeline key 包含 `MaterialTemplate`、technique、pass、shader reflection layout 和 render target signature，不包含具体 `MaterialInstance` 参数值。
- texture/sampler/spectrum/bsdfTable 等资源仍通过全局 bindless table slot 引用；template storage 中保存 slot/index，不保存资源路径。
- shader 只读取当前 pipeline/template 对应的 material storage layout；不在 shader 内解析跨 BSDF 的大 variant 结构。
- 如果某个 pass 的 shader reflection 与该 `MaterialTemplate` 的参数 schema 不匹配，technique validation 失败并报告 material URI、template type、pass 和字段路径。

### R9: Indirect Draw

本轮 SHALL 支持 indirect draw 作为 bindless 渲染路径的一部分。

要求：

- render work build 阶段按 technique/pass/pipeline/template 分组生成 draw data。
- 每个 draw data 记录 object index、material index、mesh/geometry index 和必要 instance range。
- GPUResourceTable 上传 indirect draw command buffer。
- Vulkan backend 使用 indirect draw 执行可支持的 pass。
- 不支持 indirect 的 backend 或 pass 必须报 unsupported，而不是悄悄走旧 forward path。

首版可以由 CPU 生成 indirect command buffer；GPU culling / GPU-driven command generation 不在本 REQ 范围内。

`REQ-071-d` SHALL 同时清理 `REQ-071-a/b/c` 中为保持 smoke 可用而保留的非 bindless transitional draw path。bindless + indirect draw 路径可用后，Forward/Deferred/OfflineRT 的新 technique 不得继续依赖旧 per-material descriptor 或非 bindless draw submission。调试路径如果保留，必须明确标记为 debug-only，不能参与默认 smoke。

完成本 REQ 时，代码层面不得再保留会被正常渲染流程调用的非 bindless resource bridge / draw submission / per-material descriptor 更新路径。相关旧入口要么删除，要么被隔离到显式 debug-only 编译或运行开关下；默认 editor、offline smoke 和 technique switch 都不能触达这些入口。

### R10: MaterialTemplate 批量绘制

同一 `MaterialTemplate` / technique / pass 下的多个 `MaterialInstance` SHOULD 共享 pipeline，并通过 material index 读取不同参数。

要求：

- pipeline key 不包含具体 material 参数值。
- material instance 差异进入 material array / reflected parameter storage。
- 同 template 多 instance 不应产生多套 pipeline。
- draw sorting / indirect command 生成应优先按 pipeline/template 分组。

## 测试

### T1: Pipeline Cache Find / GetOrCreate

验证 `find` 不创建，`getOrCreate` miss 创建且可观测，preload 幂等。

### T2: Cache Blob Round Trip

Vulkan backend 导出 pipeline cache blob，再导入到新 table；验证 cache import 被调用，失败路径有 warning。

### T3: Upload Task Progress

构造 mesh + texture + material scene，验证 task graph 输出 expected task 列表、依赖和 progress。

### T4: Technique Switch Loading State

editor 切换 Forward/Deferred：

- 渲染暂停。
- progress/log 窗口出现。
- 任务完成后恢复。
- 失败时 scene 不进入半渲染状态。

### T5: No Backend Type Leak

core/RHI 接口头文件不包含 Vulkan 类型。

### T6: Bindless Material Array

两个 object 使用同一 `MaterialTemplate` 的不同 `MaterialInstance`：

- 共用同一 pipeline。
- draw data 指向不同 material index。
- shader 从 material array 读取不同参数。

### T6.1: Global Bindless Slot Mapping

两个 material instance 引用同一个 texture URI，另一个 material instance 引用不同 texture URI：

- `SceneResourceTable` snapshot 中共享 texture 只有一个 resource handle / typed texture index。
- `GPUResourceTable` 为共享 texture 只分配一个 texture table slot。
- 两个 material GPU records 保存同一个 texture slot。
- 不创建 per-material descriptor set 来表达 texture 绑定。

### T7: Indirect Draw Execution

构造多个 mesh/object：

- CPU 生成 indirect draw buffer。
- Vulkan backend 使用 indirect draw。
- draw count 与 visible object/work item 数一致。

### T8: Unsupported Bindless Capability

关闭或模拟缺少 bindless/descriptor indexing 支持：

- validation 报 unsupported。
- 不静默回退到 legacy forward descriptor path。

### T9: Helmet Rendering Smoke Gate

本 REQ 完成时 SHALL 继续运行 helmet editor/offline smoke：

- helmet scene 走 GPUResourceTable upload。
- bindless + indirect draw path 至少可渲染 helmet Forward direct。
- editor realtime 输出非全黑。
- offline direct 输出非全黑。
- smoke SHALL 走 bindless + indirect draw path；不得依赖 legacy forward path 或非 bindless transitional draw path。

### T10: Transitional Draw Cleanup

验证 `REQ-071-a/b/c` 中记录的 transitional draw/resource bridge：

- 默认 realtime smoke 不再调用这些路径。
- 新 technique 不再生成 legacy per-material descriptor draw item。
- 旧 material loader / 旧 PBR 参数路径不参与渲染。
- 正常代码路径中不再调用非 bindless resource bridge / draw submission / per-material descriptor 更新。
- 若保留 debug-only draw path，必须有显式 debug flag，默认关闭，并有测试确认 smoke 未使用它。

## 修改范围

- `src/core/rhi/`：GPUResourceTable 接口、handle、cache metadata。
- `src/backend/vulkan/`：Vulkan GPUResourceTable、pipeline cache 迁移/适配。
- `src/core/frame_graph/`：pipeline desc collection 与 task 输入。
- `src/core/scene/` / `src/core/resource/`：bindless upload view、draw data、material/object indirection。
- `src/demos/lxe_editor/`：loading/progress/log UI。
- `src/core/task/` 或相近目录：简单 task/job 基础。
- `src/test/`：cache、task、editor loading state 测试。

## 边界与约束

- 本 REQ 不实现完整 work-stealing job system。
- 本 REQ 要求本轮 bindless + indirect draw 路径可用，并清理前序非 bindless transitional draw path。legacy/debug path 如保留，必须默认关闭且不能参与新 technique smoke。
- 本 REQ 不实现 scene package 文件格式；只提供 cache/upload 接口。
- 本 REQ 不改变 material parameter contract。
- 本 REQ 不实现 GPU culling 或 GPU-generated indirect commands；indirect command buffer 可由 CPU 生成。

## 依赖

- `REQ-071-b`：technique/pass validation 产出 pipeline build descs。
- `REQ-071-c`：SceneResourceTable 资源图和 parser 拆分。
- `REQ-069-a`：realtime renderer 拆分让 executor/resource manager 更容易迁移。

## 后续工作

- `REQ-071-e`：scene package 保存 CPU resource graph 和 backend cache metadata。
- `REQ-071-f`：加载 helmet/BMW 并验证切 technique / 渲染输出。
- 本 REQ 内清理前序为保持 helmet smoke 可用而保留的 legacy GPU upload / descriptor bridge，不后置到 `REQ-071-f`。

## 实施状态

未实施。本文档用于确认 GPUResourceTable、pipeline cache 和 upload task 设计边界。
