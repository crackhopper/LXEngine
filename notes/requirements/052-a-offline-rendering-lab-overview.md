# REQ-052-a: Offline Rendering Lab 总览

> 2026-06-01 新增：本 REQ 定义 LXEngine 离线渲染实验场的总体方向。当前仍在讨论中，作为后续 `REQ-053-a` 到 `REQ-059-a` 的架构锚点。

## 背景

LXEngine 当前已经具备 Vulkan 实时渲染、FrameGraph、材质系统、HDR/Post、PBR/IBL 和场景 YAML。`REQ-048-a` 到 `REQ-051-a` 已经把静态 HDR 环境 IBL 跑通，但它仍是实时渲染路径的一部分：加载场景时用 GPU 预处理环境贴图，再由 Forward/PBR shader 消费。

下一步需要一个 **Offline Rendering Lab**：它能对同一份场景用更高质量或更可控的方式渲染，输出 ground truth 图像，随后再扩展为 bake asset generator、editor 预览入口和论文/算法实验场。

外部参考：

- Unreal Path Tracer：在引擎场景内提供高质量 path tracing view，用于 final render 和 realtime ground truth 对比。
- Unreal GPU Lightmass / Unity Progressive Lightmapper：使用 path tracing 思路生成可回灌实时管线的 baked lighting。
- NVIDIA Falcor：面向实时/离线渲染研究的模块化框架，适合作为 integrator/pass 实验模型参考。
- pbrt / Mitsuba：强调物理正确、场景描述、积分器和采样模型，适合作为 reference renderer 的理论参考。

## 目标

1. 建立 LXEngine 自己的离线渲染实验场，而不是旁路 demo。
2. 复用现有 scene、asset、material、math、Vulkan backend 能力。
3. 第一阶段优先输出 ground truth 图像。
4. 第二阶段生成可回灌实时引擎的 bake 资产。
5. 第三阶段接入 editor。
6. 第四阶段形成研究 sandbox，支持论文复现和实时化实验。
7. 最终提供一张高质量离线 ray tracing 渲染图，作为实时 renderer 的视觉和数值参考。

## 总体路线

优先级固定为：

1. **A: Ground Truth Image Renderer**
   - 第一版用 Vulkan compute 离线渲染器输出 EXR + PNG。
   - 不做 CPU path tracer 起步。
   - 不要求 Vulkan ray tracing pipeline 起步。
2. **B: Bake Asset Generator / PBR Reference**
   - 支持更完整的 PBR、纹理、多 bounce、progressive accumulation。
   - 生成 probe / lightmap / cubemap 等可回灌实时管线的数据。
3. **D: Editor Integrated Preview**
   - editor 里触发离线渲染 job，显示进度和结果。
4. **C: Research Sandbox**
   - integrator/pass 可插拔，服务论文复现、算法对比和实时化前验证。

## 需求

### R1: Offline Rendering Lab 是引擎能力，不是独立玩具 renderer

离线渲染器必须复用当前引擎事实：

- `.scene.yaml`
- `Scene` / scene document / scene runtime
- mesh / texture / material loader
- core math / ray / camera / transform
- Vulkan device/resource/shader/pipeline 基础设施

离线 renderer 可以有自己的 `SceneResourceTable`、`GpuScene`、BVH 和 integrator，但不能另起一套长期分叉的资产格式。

### R2: 第一版 backend 路线为 Vulkan compute

第一版离线渲染器使用 Vulkan compute shader 跑通：

- headless Vulkan device
- storage buffer scene data
- CPU 构建 BVH，GPU compute 遍历
- accumulation / output buffer
- GPU readback
- EXR + PNG 输出

硬件 ray tracing extension 是后续 acceleration backend，不属于第一版。

### R3: 阶段需求拆分

本总览下拆分：

| REQ | 主题 |
|---|---|
| `REQ-053-a` | scene YAML 与 offline render profile |
| `REQ-053-b` | assets-downloader external resource importer |
| `REQ-054-a` | Vulkan renderer realtime/offline 拆分 |
| `REQ-054-b` | Vulkan compute offline renderer MVP |
| `REQ-055-a` | EXR + PNG 输出 |
| `REQ-056-a` | offline PBR texture material support |
| `REQ-057-a` | PBR path tracing reference |
| `REQ-058-a` | editor integration |
| `REQ-059-a` | research integrator sandbox |

Bake asset generator 不进入当前 active 实施队列，已拆到 planned 文档：

| Planned route | 主题 |
|---|---|
| `bake-reflection-probe-plan.md` | reflection probe bake |
| `bake-irradiance-probe-sh-plan.md` | irradiance probe / SH bake |
| `bake-lightmap-plan.md` | lightmap bake |

### R4: 统一术语

| 术语 | 含义 |
|---|---|
| `SceneResourceTable` | renderer-neutral 的离线场景中间表示 |
| `GpuScene` | 上传到 Vulkan compute shader 的 buffer 布局 |
| `Integrator` | 渲染算法，如 camera ray、path tracing、probe bake |
| `AccelerationBackend` | 加速结构后端，如 compute BVH、未来 Vulkan RT |
| `RenderProfile` | `.scene.yaml` 中的离线渲染参数集合 |
| `AOV` | debug/analysis 输出通道，如 normal、albedo、depth |

## 修改范围

- `notes/requirements/`
- `notes/concepts-design/`
- 后续 REQ 会分别触及 `src/core/`、`src/infra/`、`src/backend/vulkan/`、`src/demos/lxe_editor/`、`assets/`

## 边界与约束

- 本 REQ 不直接实现代码。
- 本 REQ 不要求一次性实现所有阶段。
- 本 REQ 不引入独立 scene 格式。
- 本 REQ 不把 CPU path tracer 作为第一版路线。
- 本 REQ 不要求第一版使用 `VK_KHR_ray_tracing_pipeline`。

## 依赖

- 当前 scene / asset / material / Vulkan backend 能力
- `REQ-046-a` 到 `REQ-051-a`
- `openspec/specs/renderer-backend-vulkan/spec.md`
- `openspec/specs/material-system/spec.md`
- `openspec/specs/frame-graph/spec.md`

## 后续工作

- 按 `REQ-053-a` 到 `REQ-059-a` 分阶段推进。
- 后续可以补充 roadmap，把 Offline Rendering Lab 纳入长期渲染研究主线。

## 实施状态

讨论中，未开始。
