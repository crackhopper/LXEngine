# 需求文档目录约定

本目录存放**进行中**的需求文档（in-flight REQs）。已落地需求归档到 `finished/`，已确认后置但仍有价值的需求放到 `pending/`。已经讨论清楚、但明确不进入当前 active 实施批次的技术路线放到 `planned/`。自动生成的导航索引在 `index.md`（由 `scripts/notes/generate_site_config.py` 维护，不要手编）。

> 自动生成的 `index.md` 与本手写 `README.md` 各司其职：前者列出所有进行中的 REQ 文件名 + 标题，后者解释**约定**与提供**实施顺序快照**。

## 编号约定（自 2026-05-01 起）

**REQ 文件号 = 实施顺序**。读 REQ 列表时直接按文件号升序就是预期实施顺序。

具体规则：

1. **文件号即实施顺序**：`034 < 035 < 036 ...` 表示前者应早于后者实施。打开目录看到 `034-foo.md` / `035-bar.md` / `036-baz.md`，意思就是 foo 先做、bar 次之、baz 最后。

2. **一个 REQ 文件 = 一个连续实施周期**：同一文件不要同时包含“现在就做”和“等另一个新需求之后再做”的两段工作。若旧 REQ 横跨多个实施窗口，先拆文件，再排编号。

3. **新 REQ 取号位置**：起草新 REQ 时，先在大脑里把它跟现有 pending REQ 排个实施顺序：
   - **预期最后实施** → 取下一个连号（`max(existing) + 1`）
   - **预期插入某个 pending REQ X 的同一实施槽** → 优先使用后缀族，不整体后移后续编号。例如把 `020-foo.md` 拆成 `020-a-foo.md`、`020-b-bar.md`
   - **预期排在某个 pending REQ X 之前，且不是 X 的拆分 / 补充** → 只有确认这是全局顺序变化时，才给 X 之后的 pending REQ 编号 +1
   - **预期跟某 pending REQ Y 大致同步** → 取 Y 后面一个连号
   - **预期只让旧需求的一部分后置** → 先拆旧需求，再用同一数字前缀的后缀表达顺序

4. **拆分旧需求**：如果旧需求横跨多个实施周期，把已经可以先做的 R 项留在 `NNN-a`，把后置 R 项移到 `NNN-b` / `NNN-c` 等新的 active REQ。新旧文档都写一段简短说明，记录拆分来源、拆分日期、保留范围和移出范围。历史上已经存在的 `NNNa` 文件不强制迁移；新文件统一使用 `NNN-a-*.md`。

5. **后缀顺序**：同一数字槽内按 `020-a < 020-b < 020-c < 021` 排序。不要让 active 目录里同时存在未拆分的 `020-foo.md` 和同槽的 `020-a-*`；一旦拆分，原 `020` 也改成 `020-a`。

6. **实施顺序变化时的重排**：当依赖图变了（新依赖、新优先级），如果导致现有 pending 文件号顺序与实施顺序不一致，先判断能否用局部后缀族表达。只有不能局部表达时才**重排数字号**：
   - 调整需要的 `mv` 命令：`mv NNN-slug.md MMM-slug.md`
   - 同步更新所有引用
   - 不依赖历史变更归档作为当前事实来源；以当前代码、当前需求和当前设计文档为准

7. **`pending/` 表示后续候选，不是 active**：当某个需求仍有价值但不属于当前连续实施周期，把它移到 `pending/`，并在文档顶部和“实施状态”说明后置原因。`pending/` 中的编号是历史锚点，不参与 active 目录的实施顺序。

8. **`planned/` 表示已讨论的未来计划，不是 active**：当某条路线需要保留原理、数据流和实施步骤，但本轮明确不实现，把它放到 `planned/`。`planned/` 文档不占用 `REQ-NNN-a` 编号；未来真正进入 active 实施时，再按当时队列重新取号并迁出或改写。

9. **`finished/` 只保留近期工作记录**：已归档需求的文件号是当时完成顺序的快照，**不**回溯重排。`finished/` 不是永久历史库；过时需求应下沉到 subsystem / concept / roadmap / spec，目录内原则上只保留最近约 10 个仍有直接协作价值的需求。

10. **speculative 候选编号用字母**：研究文档中的“未来 REQ”用字母占位（`REQ-A`、`REQ-B` ...），不要占用数字号。数字号留给真正落地到 `notes/requirements/`、`pending/` 或 `planned/` 的文件。

## 当前 active REQ（实施顺序快照，2026-06-14）

2026-06-14 复核 active 文件后，把旧 `REQ-054-a`、`REQ-057-a`、`REQ-058-a`、`REQ-059-a`、`REQ-067-a`、`REQ-068-a`、`REQ-071-b`、`REQ-071-c` 按当前代码事实归档、合并或重排。大文件拆分类需求合并后置到 `REQ-076-d`，3DGS 链路按当前代码事实重排到队尾 `REQ-077-a` 到 `REQ-077-e`。

| REQ | 主题 | 实施窗口 |
|---|---|---|
| `REQ-053-b` | Assets Downloader 外部资源下载与导入工具 | 外部/内置资产下载、导入、转换、路径管理 |
| `REQ-073-e` | Indirect Material Batching And Diagnostics | indirect batching；本轮未处理 |
| `REQ-073-f` | Realtime Material Path Hard Cut And Smoke | realtime clean gate；本轮未处理 |
| `REQ-073-g` | OfflineRT RenderPathGraph Compute Path | OfflineRT graph path；本轮未处理 |
| `REQ-073-h` | OfflineRT Config Hard Cut And Smoke | OfflineRT config hard cut；本轮未处理 |
| `REQ-073-i` | Specialized PBRT BSDF Contracts | PBRT 高阶材质 source；本轮未处理 |
| `REQ-074-a` | Texture Compression Pipeline With BC7 | BC7 压缩；本轮未处理 |
| `REQ-074-b` | Package Canonical State Readiness Gate | package 前 clean gate；本轮未处理 |
| `REQ-074-c` | LxScenePackage File Format | package 文件格式；本轮未处理 |
| `REQ-074-d` | SceneResourceTable Package Serialization And Restore | CPU package restore；本轮未处理 |
| `REQ-074-e` | GPU Pipeline Cache Package Metadata And Vulkan Restore | GPU pipeline cache restore；本轮未处理 |
| `REQ-074-f` | BMW M6 Package Load Performance Comparison | package 性能验收；本轮未处理 |
| `REQ-074-g` | Post-package Hard Cut And Cleanup | package 后 hard cut；本轮未处理 |
| `REQ-075-a` | Offline Realtime Equivalence On New Architecture | 新架构等价验证；本轮未处理 |
| `REQ-076-a` | Advanced Offline Path Tracing Reference | Material v3 / OfflineRT 稳定后的完整 path tracing reference |
| `REQ-076-b` | Editor Offline Render Integration | editor 触发、取消、查看离线 job |
| `REQ-076-c` | RenderPathGraph / Material / Effect Non-Mesh Rendering Extension | 3DGS 等非 mesh 渲染架构前置 |
| `REQ-076-d` | Large File Decomposition Backlog | 合并 Vulkan realtime / core commands / SceneRuntime 大文件拆分 |
| `REQ-077-a` | 3DGS PLY Loader 与 CPU Resource | 3DGS 数据解析；队尾后置 |
| `REQ-077-b` | 3DGS Runtime Resource 与 Scene Node | Scene/runtime 接入；队尾后置 |
| `REQ-077-c` | 3DGS Vulkan Splat Pass | 首个可视化渲染闭环；队尾后置 |
| `REQ-077-d` | 3DGS Editor Scene Validation | Editor 验收；队尾后置 |
| `REQ-077-e` | 3DGS System Design And Tutorial | 文档收口；队尾后置 |
| `REQ-078-a` | Async FrameGraph Execution And Multi-Queue Synchronization | realtime/offline/3DGS 可用后的异步执行、barrier plan 与多 queue 性能升级 |

## 当前 planned REQ（已讨论，当前不实现，2026-06-01）

以下需求记录未来路线的原理和实施步骤，但不进入当前 active 实施批次：

| 计划 | 主题 | 后置原因 |
|---|---|---|
| `bake-reflection-probe-plan.md` | Reflection Probe Bake | 依赖 ground truth renderer 稳定，本轮只记录计划 |
| `bake-irradiance-probe-sh-plan.md` | Irradiance Probe / SH Bake | diffuse probe/SH 与 reflection probe 分开规划 |
| `bake-lightmap-plan.md` | Lightmap Bake | UV2、atlas、texel visibility 复杂度高，单独后置 |

## 当前 pending REQ（后续候选，2026-05-17）

以下需求已从 active 队列移入 `pending/`，避免 v0.1.1 开发同时引入过多方向：

| REQ | 主题 | 后置原因 |
|---|---|---|
| `REQ-043` | 内置 OBJ 资产材质槽与 MTL 颜色支持 | 资产质量修补，不是 v0.1.1 主线前置 |
| `REQ-044-a` | Web Editor Shell 与 IPC 合同 | Phase 9 后续 |
| `REQ-044-b` | Engine CLI / MCP / Agent 入口 | Phase 10 后续 |
| `REQ-044-c` | Editor AssetRegistry 与热重载桥接 | Phase 3 后续 |

## 本轮删除的 active REQ（2026-05-11）

以下文档已从 active 队列删除，因为它们与当前“快速搭测试场景文件”的目标不再同向，或者建立在与现状不一致的架构假设上：

- `041-g-component-v2-multi-and-enable.md`
- `041-h-mesh-level-triangle-picking.md`
- `041-i-debug-draw-persistent-and-mesh.md`
- `041-j-component-dependency-declaration.md`

另外，旧的 `041-d` / `041-f` 内容已改写并重命名为新的 scene authoring 需求，原“undo/redo UI / 菜单栏与主题”不再保留为 active REQ。

## 历史

- 2026-06-14：删除 active `REQ-072`。原 071 收口审计、legacy hard cut、bindless validation、package 和 equivalence 工作已由后续 active `REQ-073-*`、`REQ-074-*`、`REQ-075-a`、`REQ-076-*` 承接，不再保留独立 active 需求；同步删除 072 专属 Superpowers spec / plan。
- 2026-06-14：移除旧 tutorial registry pending 候选 `REQ-042-a/b/c`。教程和 editor design 改为只讲当前 command-first、scene-node、light 数据/shader 边界；light asset/custom light registry、toolbar registry、node kind registry 不再作为当前 roadmap 或教程 future workflow 暴露。
- 2026-06-14：继续整理 active 顺序。`REQ-054-a` 归档，offline graph/default path 并入 `REQ-073-g`，old config bridge 删除并入 `REQ-073-h`，renderer 大文件拆分并入 `REQ-076-d`。旧 `REQ-057-a`、`REQ-058-a`、`REQ-059-a` 重排为 `REQ-076-a`、`REQ-076-b`、`REQ-076-c`，其中 `REQ-076-c` 改写为支持 3DGS 等非 mesh 渲染结构的 RenderPathGraph/material/effect 架构扩展。`REQ-067-a`、`REQ-068-a`、`REQ-071-b`、`REQ-071-c` 归档，剩余事项由 `REQ-073-*`、`REQ-074-*`、`REQ-075-a`、`REQ-076-b/c` 承接。
- 2026-06-14：新增 `REQ-078-a`，把 FrameGraph barrier plan、split barrier、timeline semaphore、multi-queue scheduling、async compute 和 secondary command buffer parallel recording 集中后置到 realtime / OfflineRT / 3DGS 可用闭环之后，避免打断 `REQ-073-*` 短期渲染目标。
- 2026-06-14：继续整理 active 顺序。旧 `REQ-069-a/b/c` 合并为 `REQ-076-d` 并移到 `REQ-076-c` 之后；3DGS active 链从旧 `REQ-061-a` 到 `REQ-065-a` 重排为 `REQ-077-a` 到 `REQ-077-e`，并按当前代码事实修正为“只有 assets-downloader cache 表面，尚无 loader/runtime/render/editor 闭环”。
- 2026-06-14：按当前代码复核 `REQ-072` 之前的 active 需求。已完成或已由当前架构取代的 `REQ-045-a/b/c`、`REQ-046-a` 到 `REQ-052-a`、`REQ-053-a`、`REQ-054-b`、`REQ-055-a`、`REQ-056-a`、`REQ-060-a`、`REQ-063-a`、`REQ-066-a`、`REQ-067-b`、`REQ-070-a`、`REQ-071-a/d/e/f/g` 归档到 `finished/`；仍未完成的 `REQ-053-b`、`REQ-054-a`、`REQ-057-a` 到 `REQ-059-a`、`REQ-061-a`、`REQ-062-a`、`REQ-063-b` 到 `REQ-065-a`、`REQ-067-a` 到 `REQ-069-c`、`REQ-071-b/c` 当时保留在 active，并同步实施状态到当前代码事实。后续同日整理又把其中一部分继续归档或重排。
- 2026-06-10：新增 `REQ-071-a` 到 `REQ-071-f`，把 SurfaceMaterial pure envelope、RenderPathGraph/RenderFeature、SceneResourceTable parser/resource ownership、GPUResourceTable/pipeline cache/upload task、scene package 和 helmet/BMW offline-realtime 渲染等价验证收敛为一个连续需求族。该族承接 `REQ-067-a/b` 的 SceneResourceTable 资源模型和 `REQ-070-a` 的 BMW M6 转换输入，目标是先把材质/渲染合同说清楚，再实现加载性能和对齐验收。
- 2026-06-01：新增 `REQ-052-a` 到 `REQ-059-a`，建立 Offline Rendering Lab 主线。路线优先级为 Ground Truth Image Renderer、Bake Asset Generator / PBR Reference、Editor Integrated Preview、Research Sandbox；第一版选择 Vulkan compute 离线 renderer，不以 CPU path tracer 或 Vulkan hardware RT pipeline 起步。随后将 `REQ-054` 拆成 `054-a` renderer foundation/realtime/offline 拆分与 `054-b` compute offline renderer MVP，避免继续扩大当前 2200+ 行 `VulkanRendererImpl`。补充 `REQ-053-b` `assets-downloader`，管理大型网络资产下载、导入、转换和 scene 路径引用，避免 git 仓库膨胀。Bake asset generator 暂不进入 active 实施队列，拆入 `planned/`，记录 reflection probe、irradiance/SH 和 lightmap 的原理与计划；未来执行时再重新取 active REQ 编号。
- 2026-06-01：主干合并 Offline Rendering Lab 与 3DGS PLY 两条 active 主线时，保留 Offline Rendering Lab 的 `REQ-052-a` 到 `REQ-059-a` 编号，将 3DGS PLY 支持主线顺延为 `REQ-060-a` 到 `REQ-065-a`，避免 active 目录出现重复编号。
- 2026-05-28：新增 `REQ-060-a` 到 `REQ-065-a`，建立 3DGS PLY 支持主线：先引入 Apache-2.0 的 train scene PLY 样例并调整资产预算，再拆分 loader、runtime、通用 compute pipeline 前置、Vulkan splat pass、editor 验收和系统设计 / 教程文档。
- 2026-05-26：新增 `REQ-046-a` 到 `REQ-051-a`，建立 HDR/Post、HDR texture/cubemap、IBL GPU bake、PBR IBL material、金属球测试场景和教程主线；新主线槽位默认从 `NNN-a` 起步，后续同槽位补充再使用 `NNN-b` / `NNN-c`。
- 2026-05-17：`REQ-042-a/b/c` 与 `REQ-043-a/b` 完成验证并归档到 `finished/`；active 队列清空，新增 `notes/concepts-design/rendering-pipeline/` 概念章节解释 FrameGraph、Render Target、Shadow Pass、CSM 与 RenderQueue。
- 2026-05-17：按 v0.1.1 目标重整 active 队列。新增 `REQ-042-a/b/c` 收口 FrameGraph v1、Directional Shadow、CSM；新增 `REQ-043-a/b` 收口 shadow 阶段教程支撑与架构概念文档展开。原教程扩展、OBJ 材质槽、Web Editor、Engine CLI/MCP、AssetRegistry 热重载需求移入 `pending/`。
- 2026-05-14：`REQ-041-d` 到 `REQ-041-h` 完成验证并归档到 `finished/`。`lxe_editor` 已具备测试场景 primitive/camera/light 创建、Inspector 材质/可见性编辑、节点复制、typed light、RTR 实验材质模板与节点级参数覆盖。
- 2026-05-16：补充 `REQ-044-a/b/c`，把概念与设计文档中引用的 Web Editor、engine MCP/CLI/agent、AssetRegistry/热重载 roadmap 内容明确标注为未实施需求；这些需求在 2026-05-17 已移入 `pending/`。
- 2026-05-14：按当前代码复核 active REQ，补充每个需求的“当前代码对照”，澄清已存在的 camera / directional light / Inspector / helper overlay 基础能力与仍待实现的 scene authoring 能力边界。
- 2026-05-13：清理 `finished/`，删除 `REQ-034` 及以前的过时归档，只保留近期 10 个需求（`REQ-035` 到 `REQ-041-c`）。仍有价值的上下文下沉到 concept / subsystem / roadmap / spec，未完成存疑点记录到 `tmp/notes/unfinished-finished-requirements.md`。
- 2026-05-11：按当前代码与目标重整 active REQ。删除 `041-g`~`041-j`，并把保留需求改写为面向测试场景搭建的 `041-d` / `041-e` / `041-f`。
- 2026-05-11：在 `041-f` 后新增 RTR 第五章实验底座需求：`041-g` 多类型光源数据与作者入口、`041-h` 实验材质接入与节点级参数覆盖。两者只定义环境能力，不实现具体光照公式或 Gooch shader。
- 2026-05-08：`REQ-041-c` 编辑器多选 / 框选完成验证并归档到 `finished/041-c-editor-multi-select.md`。
- 2026-05-07：`REQ-040-a` Editor 命令总线完成验证并归档到 `finished/040-a-editor-command-bus.md`，pending 队列从 `REQ-041-a` 开始。
- 2026-05-07：`REQ-039-a` DebugDraw 子系统完成验证并归档到 `finished/039-a-debug-draw-subsystem.md`。
- 2026-05-06：`REQ-041-a` ImGui Editor MVP、`REQ-041-b` 命令总线 v2 相关前置逐步稳定，随后开始进入 v2 需求拆分期。
- 2026-05-01：建立“REQ 文件号 = 实施顺序”约定。

## 相关

- `index.md`：自动生成的 active REQ 列表（导航用）
- `pending/`：已后置但仍保留的后续候选
- `planned/`：已讨论清楚但当前不实现的技术路线计划
- `finished/`：归档需求（按时间快照不重排）
- `notes/roadmaps/`：跨 REQ 的优先路径与阶段编排
