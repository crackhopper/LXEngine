# Phase 11 · AI 资产生成

> **目标**：把 AI 生成能力融入资产管线 —— 一句话产出贴图 / 3D 模型 / 动画 / 角色 / 环境。**引擎成为内容生产工具**，而不只是消费工具。
>
> **依赖**：Phase 3（asset registry）、Phase 10（agent + MCP tools 作为调用入口）。
>
> **可交付**：在 `engine-cli --chat` 或编辑器控制台里：
> - 『给我一只木桶』→ 场景里出现一只 PBR 贴图齐全的木桶
> - 『给这个角色做个挥手动画』→ 角色播放一段新生成的动画
> - 『给主角换个科幻风格外观』→ 角色网格 + 贴图全换
> - 『我有一段手机拍的房间视频，把它变成游戏可用的 3D 场景』→ 走 NeRF / 3DGS 管线

## 范围与边界

**做**：
- AI 生成 pipeline 框架（抽象所有"生成服务"为统一接口）
- 贴图生成（text-to-image / image-to-image / PBR 贴图分解）
- 3D 模型生成（text-to-3D / image-to-3D / sketch-to-3D）
- 动画生成（text-to-motion / motion capture from video）
- 角色生成（character creator + AI 风格化）
- Environment / skybox 生成（HDR panorama）
- NeRF / 3DGS 导入（photo/video → radiance field → 可渲染资产）
- 生成产物的后处理（retopology / texture atlasing / LOD）
- 成本 / 延迟追踪

**不做**：
- 自己训练 / 微调生成模型（直接用开源 / 商业服务）
- 跨模态大模型训练
- 模型 hosting（支持连接远程服务，不自己跑服务器）

---

## 前置条件

- Phase 3：资产注册 + GUID
- Phase 10：skills registry 可扩展 + agent 可调用

---

## 架构总览

每个生成能力都是一个 **generator**，统一接口（抽象）：

- `capability()` → 描述自己的 name / 输入 schema / 输出类型 / 给 LLM 读的用途描述
- `estimateCost(request)` → 预估 token / 时间 / USD / GPU 秒（契合 [P-9](principles.md#p-9-成本模型是一等公民)）
- `generate(request, progressCallback)` → 异步执行，返回一个资产 handle

Generator 的实现分三大类：

1. **远程 API 型**：调用外部商业 / 开源服务
2. **本地推理型**：在本机 GPU 上跑开源模型
3. **离线工具型**：包装命令行工具（3D 重建 / 格式转换）

每个 generator 注册到 `AssetGeneratorRegistry`，**自动进入** Phase 10 的 skill tree（[P-14 能力发现](principles.md#p-14-能力发现优于能力配置)）。新增一个 generator = 新增一条 skill，agent 立即可用，不改 agent 代码。

**统一的产物契约**：
- 生成完成后自动写入资产注册表 + 登记 provenance（[P-10](principles.md#p-10-资产血统--provenance)）
- 产生的资产 handle 与手动导入的资产在下游接口上无区别（agent 不需要区分"这是生成的还是导入的"）

---

## 工作分解

### REQ-1101 · Generator 框架

- `IAssetGenerator` 接口 + `AssetGeneratorRegistry`
- Async 调用：通过 `std::future` 或 coroutine，不阻塞 engine 主线程
- 进度回调：生成过程中能反馈百分比 / 阶段到 agent
- 结果直接进 asset registry：`generate()` 返回 `AssetGuid`，agent 立即可以用它
- 失败重试 + fallback：某个 provider 失败后自动切下一个

**验收**：注册一个 stub generator，agent 调用它能得到一个 mock asset。

### REQ-1102 · 贴图生成

第一类 generator。目标类别：

- **Albedo / base color**：文字描述 → 贴图
- **Normal map**：从 albedo 推或直接生成
- **PBR 全套**：一次产出 albedo + normal + roughness + metallic

**选型参考**：
- **商业 API** 路径：接入主流按次付费的 text-to-image 推理平台
- **本地推理** 路径：接入可在本机 GPU 运行的开源 diffusion 实现
- 两条路径**同时存在**，通过配置选择（REQ-1110）

数据流：
```
prompt → generator → 图像字节 → Texture loader → GPU texture → AssetHandle
```

**验收**：一条自然语言命令产出一张带 provenance 的可用贴图。

### REQ-1103 · 3D 模型生成

Text-to-3D / image-to-3D 在 2025+ 已经达到可用。Generator 输出：标准的跨平台 3D 资产格式（glTF 类），经 Phase 3 的导入器变成引擎内 Mesh + Material。

**选型参考**：有多个商业 API 和开源模型并存。商业 API 质量高但按次付费；开源模型可本地跑但需要显存。优先让两条路径都可接，通过策略选一。

**自动后处理**（可选）：
- Retopology：高模 → 游戏友好的低模
- Normal map bake：高模法线烘焙到低模
- UV atlas：检查 / 重生成 UV

**验收**：一句话生成 → 产出可在场景里渲染的 mesh，provenance 记录 prompt。

### REQ-1104 · 动画生成

两条路径：
- **Text-to-motion**：文字描述 → 骨骼动画
- **Video-to-motion**：从视频提取骨骼动画（姿态估计）

数据流：
```
prompt 或 video → generator → 标准动画格式 → AnimationClip 导入 → Phase 4 的 player
```

**关键前置工作**：**骨骼重定向**（retargeting）—— 生成模型通常输出标准骨架，需要映射到引擎里具体角色骨架。这一层是 Phase 4 的扩展。

**验收**：一句话生成 → Phase 4 的动画播放器能播放生成出的动画。

### REQ-1105 · 角色生成

角色 = 网格 + 贴图 + 骨架 + 基础动画集。可选方案：

- **端到端**：接入一个能一步到位产出角色的服务
- **组合式**：用其他 generator 分步组装（身体 → 贴图 → 骨架绑定 → 基础动画挂载）

组合式更灵活但流水线更复杂。先用端到端跑通，再做组合式作为高级模式。

**验收**：一句话生成可立即使用的角色 prefab（包含 mesh / 材质 / 骨架 / 基础动画）。

### REQ-1106 · Environment / Skybox 生成

HDR 全景图是环境光的主要来源（Phase 1 的 IBL pipeline 直接消费）。

Generator 输出：HDR 全景图 → Phase 1 的 Cubemap loader → IBL 预过滤流水线 → 立即可用。

**选型参考**：专门生成 HDR skybox 的商业服务 / 开源 panoramic diffusion 模型 / 普通 diffusion 模型 + equirect 后处理。

**验收**：一句话把场景环境光 + skybox 换掉，生效在下一帧可见。

### REQ-1107 · 神经辐射场 / Gaussian Splat 导入

这是 AI-Native 引擎的特色能力：**把真实世界采集转为游戏资产**。

两条主要路径：
- **隐式辐射场**（NeRF 家族）：训练后可导出为 mesh 或作为隐式表达直接渲染
- **显式高斯点云**（3DGS 家族）：训练产出一组带颜色 / 方差的高斯点，需要专门的 splat 渲染器

**对显式高斯点云的引擎侧工作**：
- 新增 `GaussianSplatRenderable : IRenderable`
- 新增一个 `Pass_Splat`，使用排序 + tile-based rasterization 算法（与常规 forward 不同）
- shader 可以参考现有开源实现

**选型参考**：
- 重建流水线：标准的 SfM 工具链（相机参数 + 稀疏点云）+ 神经/点云训练工具
- 在本地跑 vs 调远程服务：看显卡 + 数据量决定

数据流：

```
用户: 一组照片 / 视频
  ↓
Phase 11: SfM 工具链 → 相机参数 + 初始点云
  ↓
Phase 11: 训练工具 → 辐射场或高斯点集
  ↓
Phase 11: 导入为 Renderable
  ↓
Phase 1 / 专用 pass: 在场景里渲染
```

**验收**：一组房间照片经过 CLI 命令 → 场景里出现一个可漫游的重建场景。

### REQ-1108 · 生成流水线

复杂生成任务往往是 multi-step，需要一个流水线框架：

```
"给我一只木桶" →
  1. generate_texture(prompt="wooden planks") → albedo
  2. generate_texture(prompt="wooden planks normal map") → normal
  3. generate_texture(prompt="rough wood roughness") → roughness
  4. generate_mesh(prompt="wooden barrel with metal rings") → mesh
  5. assemble_material(albedo, normal, roughness, metallic=0) → material
  6. bind_material(mesh, material) → renderable
  7. scene.instantiate(renderable, position=near_player)
```

这整个流水线本身是一个 skill（Phase 10 REQ-1007 的扩展机制），用 TS 写，内部调用其他 skills。

- 流水线支持并行 step（前 3 步贴图生成可以并发）
- 中间产物缓存（同样 prompt 不重复生成）
- Trace 到 Phase 10 的 observability 通路

**验收**：一条自然语言指令跑完整流水线，cache 命中时秒级返回。

### REQ-1109 · 成本 / 配额

生成型 API 是真金白银。必须有成本控制：

- 每次生成前给出估算：`estimatedCostUsd()`
- 超过阈值要求用户确认（交互模式）或拒绝（headless）
- 每个 session 的总成本累计
- 每日 / 每周预算配额
- 生成失败（model error / quota exceeded）走 Phase 2 的 `EngineError`，agent 能看到并自动 fallback

**验收**：超出配额后 generator 返回错误，agent 选择另一个更便宜的 provider。

### REQ-1110 · 路由策略

每种生成能力都可以有**多个 generator 并存**，由策略在运行期选择：

- `prefer_local` — 能本地跑就本地跑（省钱、隐私）
- `prefer_cheapest` — 按成本估算挑最便宜的
- `prefer_fastest` — 按延迟估算挑最快的
- `prefer_quality` — 挑质量最高的（通常最贵）
- `round_robin` — 轮询，平均负载 / 账单

配置驱动，agent 可以在对话里显式指定（"用最便宜的方案给我一张贴图"）。

**验收**：切换 `prefer` 策略后同一条生成命令走向不同的 generator，产出的 provenance 字段反映实际路径。

---

## 里程碑

### M11.1 · Generator 框架

- REQ-1101 完成
- demo：stub generator 被 agent 调用成功

### M11.2 · 贴图生成

- REQ-1102 完成
- demo：`agent> 给我一张砖墙贴图`，得到 PBR 贴图全套

### M11.3 · 3D 模型 + 动画

- REQ-1103 + REQ-1104 完成
- demo：`agent> 给我一只挥手的熊`，场景里出现动的模型

### M11.4 · 角色 + 环境

- REQ-1105 + REQ-1106 完成
- demo：一句话生成角色 + 换环境光

### M11.5 · NeRF / 3DGS

- REQ-1107 完成
- demo：手机视频 → 可漫游的 splat 场景

### M11.6 · 流水线 + 成本

- REQ-1108 + REQ-1109 + REQ-1110 完成
- demo：`agent> 给我一只木桶`，自动跑完 7 步流水线 + 成本控制生效

---

## 风险 / 未知

- **生成质量的不可控**：AI 生成的 mesh 可能有非流形、错误法线、破 UV。解决：每一步后跑校验 + 失败时让 agent retry / 切换 generator。
- **重建训练时间**：小场景分钟级，大场景小时级。解决：先 demo 用小场景 + 缓存。
- **API 成本失控**：一个"创建一个中世纪城镇"可能触发几十次生成。解决：[P-9 成本模型](principles.md#p-9-成本模型是一等公民) + [P-13 HITL 确认](principles.md#p-13-human-in-the-loop-是类型级契约)。
- **版权 / 来源**：生成内容的版权 / 训练数据溯源是灰色地带。解决：provenance 记录完整，发布时让用户自行决策。
- **API 版本漂移**：第三方服务的 response schema 会变。解决：每个 generator 实现版本化适配器。
- **本地模型的硬件门槛**：diffusion / 3D 生成本地跑通常要高显存。解决：本地模型可选 + 策略默认走远程。
- **评价体系**：怎么知道生成的 asset "够好"？人工评分 + provenance 字段 + 未来可接 agent 做 auto-critique。

---

## 与现有架构的契合

- Generator 是 Phase 3 asset registry 的生产者之一：和"从外部文件加载"走同一条路径，最后都是统一的 asset handle。
- 每个 generator 自动成为 Phase 10 的一个 tool，agent 可直接调用，契合 [P-14 能力发现](principles.md#p-14-能力发现优于能力配置)。
- 成本 / 进度通过 Phase 10 的 trace 通路暴露给前端。
- Splat 的新渲染路径是一个独立 pass，遵守 Phase 1 `FrameGraph` 的 pass 抽象。
- Provenance 元数据贯穿到打包发布阶段（Phase 12），让最终产物可溯源。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-9 成本模型](principles.md#p-9-成本模型是一等公民) | 每个 generator 强制 estimateCost，与 Phase 10 REQ-1012 budget 对齐 |
| [P-10 Provenance](principles.md#p-10-资产血统--provenance) | 所有生成资产记录 prompt / generator / 时间 / 成本 / 是否可复现 |
| [P-13 HITL 契约](principles.md#p-13-human-in-the-loop-是类型级契约) | 高成本生成默认 confirm 级别 |
| [P-14 能力发现](principles.md#p-14-能力发现优于能力配置) | 新接一个 generator 自动出现在 skill 清单 |
| [P-15 版本化](principles.md#p-15-重构友好--版本化的一切) | 生成资产的 schema + provenance 带版本号 |

---

## 下一步

至此，AI-Native 引擎的核心能力齐备：能读、能写、能画、能生。最后一步是打包发布到用户手上 → [Phase 12 · 打包 / 发布](phase-12-release.md)。
