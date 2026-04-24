# Phase 11 · AI 资产生成

> **目标**：把 AI 生成能力融入资产管线 — 一句话产出贴图 / 3D / 动画 / 角色 / 环境。**引擎成为内容生产工具**，而不只是消费工具。
>
> **依赖**：Phase 3（资产管线 + GUID + `.meta`）+ Phase 10（MCP tool + Agent + CLI）。
>
> **可交付**：
> - `engine-cli generate scene "indoor cafe, warm lighting"` → 由 NeRF / 3DGS 捕获资产还原整场景
> - `engine-cli generate char-portrait "elf archer"` → 2D 立绘
> - Agent 在 chat 中“给场景放一只木桶” → 自动生成 mesh + 贴图 + 材质并 instantiate

## 当前聚焦路径（2026-04-24）

优先级顺序（前置完成后直接接力）：

1. **引擎 CLI 对 AI 生成的 hook**（依赖 Phase 10 REQ-1004）
2. **NeRF / 3DGS 接入**（真实世界 → 可渲染资产；优先落地）
3. **角色立绘（2D）** — text-to-image 角色定向子集
4. **角色模型（3D）** — text-to-mesh 角色定向子集
5. **角色骨骼（2D / 3D）** — 自动 rigging
6. **角色动作（2D / 3D）** — text-to-motion + keyframe
7. **游戏 UI 生成**（HTML / Vue 子集片段）
8. 通用贴图 / 通用 3D / 环境生成（通用路径兜底）

## 当前实施状态（2026-04-24）

**未开工**。无任何 AI 资产生成基础设施。

| 条目 | 状态 |
|------|------|
| 资产生成 pipeline 框架 | ❌ REQ-1101 |
| NeRF / 3DGS 接入 | ❌ REQ-1102 |
| 角色立绘（2D） | ❌ REQ-1103 |
| 角色模型（3D） | ❌ REQ-1104 |
| 角色骨骼（2D / 3D） | ❌ REQ-1105 |
| 角色动作（2D / 3D） | ❌ REQ-1106 |
| 游戏 UI 生成 | ❌ REQ-1107 |
| 通用贴图 / 3D / 环境生成 | ❌ REQ-1108 |
| Provenance 持久化 | ❌ REQ-1109 |

## 范围与边界

**做**：生成 pipeline 框架 / NeRF + 3DGS / 角色全链路（立绘 → 模型 → 骨骼 → 动作） / 游戏 UI 生成 / 通用贴图 + 3D 兜底 / Provenance 持久化到 `.meta`（Phase 3 REQ-303）。

**不做**：训练自己的模型 / 本地推理加速（首版只调远程 API） / 生成结果的精修工作流（仍由人类在 Phase 9 编辑器完成）。

## 前置条件

- Phase 3 `AssetRegistry` + `.meta`
- Phase 10 MCP + agent runtime + CLI
- 网络（或本地 GPU 推理）

## 工作分解

### REQ-1101 · 资产生成 pipeline 框架

- 抽象 `IAssetGenerator<Input, Output>`
- 统一流程：validate input → submit → poll progress → fetch result → import to registry → persist `.meta` provenance
- Progress / cost 通过事件总线推给编辑器 / agent
- 与 Phase 10 `engine-cli` 对接：`engine-cli generate <kind> <prompt>`

**验收**：注册一个 mock generator 完成 submit → result round-trip。

### REQ-1102 · NeRF / 3DGS 接入（优先）

NeRF / 3DGS 是本 phase 第一块落地；**优先级高于 2D / 3D 文本生成**。真实世界视频 → 可渲染资产是“新旧桥梁”：

- 输入：一组图像 / 短视频 + 可选相机位姿
- 输出：
  - NeRF：隐式神经场（神经网络参数 + sampling query）
  - 3DGS：显式 Gaussian splat 点集（`position / scale / rotation / SH coeff / opacity`）
- 运行期：自定义 pass / pipeline 渲染（或转 baked cubemap / mesh 近似）

选型参考（可替换）：

- **Instant-NGP** / **nerfstudio**：NeRF 方向主流
- **[gsplat](https://github.com/nerfstudio-project/gsplat)** / **[3DGS 官方实现](https://github.com/graphdeco-inria/gaussian-splatting)**：3DGS 方向主流
- 云端 API（Luma AI / Polycam）可先作数据采集入口

引擎侧工作：

- 新资源类型 `NerfResource` / `GaussianSplatResource`（`IGpuResource` 子类）
- 自定义 `Pass_NeRFRender` / `Pass_Splat`（或复用 `Pass_Forward` + 自定义 pipeline）
- `Scene` 支持 scene-level 持有 NeRF / 3DGS 环境节点

**验收**：上传一段 30 秒视频 → 得到 3DGS 点云 → 引擎内实时渲染，与原视频视角一致。

### REQ-1103 · 角色立绘（2D）

text-to-image，但**专门针对角色**：面部特征 / 构图 / 光照 / 风格约束。

- 输入：prompt + 参考图（可选）+ 风格标签
- 输出：1–N 张高分辨率立绘 + 2D 分层（头 / 身 / 配件，用于后续 REQ-1105 2D 骨骼）

选型参考：Stable Diffusion / SDXL + ControlNet / Flux / NovelAI。

**验收**：prompt `"elf archer"` → 至少一张 1024×1024 立绘；面部特征可复现（seed 控制）。

### REQ-1104 · 角色模型（3D）

text-to-mesh，**专门针对角色**：humanoid 拓扑 / 合理 UV / PBR 贴图集。

- 输入：prompt / REQ-1103 立绘 / 多视角图
- 输出：triangle mesh + UV + PBR 贴图集（albedo / normal / metallicRoughness）

选型参考：Zero123++ / InstantMesh / Meshy / Tripo / Rodin。

**验收**：prompt `"elf archer"` → 一个 humanoid mesh（≤ 10K 面）+ PBR 贴图集；导入引擎能走 REQ-118 PBR 管线。

### REQ-1105 · 角色骨骼（2D / 3D）

自动 rigging：

- **2D**：对立绘分层骨骼化（类 Live2D / Spine）
- **3D**：对 humanoid mesh 自动骨骼绑定（auto-rigging）+ 权重刷

选型参考（可替换）：

- **3D**：Mixamo auto-rig API / RigNet / Meshy auto-rig
- **2D**：RAD 2D / 自研分层骨骼

引擎侧：

- 2D 骨骼作新资源类型 `Rig2DResource`；Phase 4 `AnimationClip` 扩展支持 2D 骨骼通道
- 3D 骨骼复用已有 `Skeleton` / `SkeletonUBO`（`src/core/asset/skeleton.*`）

**验收**：2D 立绘 / 3D 模型均可自动产出可用骨骼；骨骼导入后 `SceneNode` 能挂载并在 REQ-1106 动作下播放。

### REQ-1106 · 角色动作（2D / 3D）

text-to-motion / motion generation：

- **3D**：text-to-motion（`"walk cycle, slow"`）+ 可选 reference video keyframe
- **2D**：分层 rig 上的循环动画（idle / walk）

选型参考：Motion Diffusion Model / AnimateDiff / Cascadeur API。

引擎侧：输出直接对齐 Phase 4 REQ-401 `AnimationClip` 结构（2D / 3D 通道分别存）。

**验收**：角色全链路跑通：立绘 → 3D 模型 → 骨骼 → 动作；`scene_viewer` 挂上去能走能跑。

### REQ-1107 · 游戏 UI 生成

text-to-UI，目标产出 Phase 8 Vue 容器可直接加载的 `.vue` / `.html` 片段。

- 输入：prompt（`"HUD: hp bar top-left, score top-right, ammo count bottom"`）+ 风格标签
- 输出：`.vue` 单文件组件；`<template>` + `<style>` + 可选 `<script setup>`
- 与 Phase 8 REQ-804 UI 资产加载路径对接

选型参考：Claude / GPT-4 结构化输出 + 后处理规范化。

**验收**：prompt 生成一个 HUD `.vue` → Phase 8 UI 容器加载即可显示。

### REQ-1108 · 通用贴图 / 3D / 环境生成

前置角色管线落地后，补齐通用生成路径作兜底：

- 通用贴图：物件材质 / 地形 / 图案（复用 REQ-1103 后端）
- 通用 3D：道具 / 载具 / 建筑（复用 REQ-1104 后端）
- 环境：cubemap + lighting probe

**验收**：prompt `"wooden barrel"` → 直接可用 mesh + 贴图。

### REQ-1109 · Provenance 持久化

- 每个生成资产 `.meta` 写入 provenance（[P-10](principles.md#p-10-资产血统--provenance)）：
  ```yaml
  provenance:
    kind: generated
    generator: text_to_mesh
    input:
      prompt: "elf archer"
    model:
      provider: "..."
      version: "..."
    created_at: ...
    cost: { usd: 0.15, time_ms: 42000 }
    reproducible: true
  ```
- Agent / 编辑器通过 `query.assets.provenance(guid)` 读取

**验收**：再次 prompt 同样 `elf archer` → provenance 指示已有资产可复用。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| **M11.α · 框架 + NeRF/3DGS** | REQ-1101 + REQ-1102 | 视频 → 3DGS 实时渲染 |
| **M11.β · 角色全链路** | REQ-1103 + REQ-1104 + REQ-1105 + REQ-1106 | prompt → 可走可跑角色 |
| M11.γ · UI 生成 | REQ-1107 | 生成 HUD `.vue` |
| M11.δ · 通用兜底 | REQ-1108 | 通用物件生成 |
| M11.ε · Provenance | REQ-1109 | 生成历史可查 |

## 风险 / 未知

- **NeRF / 3DGS 渲染集成**：渲染路径与现有 forward / deferred 不同（3DGS 按 splat 排序 blend），需要独立 `Pass_Splat`；和 PBR scene 共存时排序 + 深度交互需设计。
- **角色 pipeline 质量瓶颈**：text-to-mesh 的 humanoid 拓扑通常差；auto-rig 精度不稳定。首版容忍“能用”，精修交给人在 Phase 9 编辑器。
- **模型 API 不稳定**：远程模型更迭快。抽象 `IGenerator` 隔离。
- **成本与限流**：单次 text-to-mesh 可能几美元。与 [P-9](principles.md#p-9-成本模型是一等公民) 强绑定。
- **版权与许可**：生成资产版权是灰色地带。引擎默认附加 license metadata 到 `.meta`。
- **UI 生成 vs 手写**：AI 生成 HTML 片段不保证 Phase 8 Vue 子集兼容；需要后处理校验器。

## 与 AI-Native 原则契合

- [P-9 成本模型](principles.md#p-9-成本模型是一等公民)：生成资产是最昂贵操作，预算强制必须到位。
- [P-10 Provenance](principles.md#p-10-资产血统--provenance)：所有生成资产写入 provenance。
- [P-13 HITL](principles.md#p-13-hitl-类型级契约)：生成前后人工确认。
- [P-6 Intent Graph](principles.md#p-6-intent-graph)：agent 把“放一只角色”拆成生成 pipeline 多 step（立绘 → 模型 → 骨骼 → 动作 → instantiate）。

## 与现有架构契合

- 本 phase 是 Phase 3 资产管线的“写入侧”；读入侧（loader）已就位，生成资产走同一 registry 路径。
- 3D 骨骼复用现有 `Skeleton` / `SkeletonUBO`。
- Game UI 生成产物直接走 Phase 8 UI 容器的 `.vue` 加载路径。

## 下一步

[Phase 12 打包 / 发布](phase-12-release.md)：生成资产随 build 打包；NeRF / 3DGS 体量大，需要单独的流式加载策略。
