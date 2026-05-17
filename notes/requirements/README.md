# 需求文档目录约定

本目录存放**进行中**的需求文档（in-flight REQs）。已落地需求归档到 `finished/`，已确认后置但仍有价值的需求放到 `pending/`。自动生成的导航索引在 `index.md`（由 `scripts/notes/generate_site_config.py` 维护，不要手编）。

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
   - OpenSpec 历史 change 已清理；不要依赖 `openspec/changes/archive/` 作为当前事实来源

7. **`pending/` 表示后续候选，不是 active**：当某个需求仍有价值但不属于当前连续实施周期，把它移到 `pending/`，并在文档顶部和“实施状态”说明后置原因。`pending/` 中的编号是历史锚点，不参与 active 目录的实施顺序。

8. **`finished/` 只保留近期工作记录**：已归档需求的文件号是当时完成顺序的快照，**不**回溯重排。`finished/` 不是永久历史库；过时需求应下沉到 subsystem / concept / roadmap / spec，目录内原则上只保留最近约 10 个仍有直接协作价值的需求。

9. **speculative 候选编号用字母**：研究文档中的“未来 REQ”用字母占位（`REQ-A`、`REQ-B` ...），不要占用数字号。数字号留给真正落地到 `notes/requirements/` 的文件。

## 当前 active REQ（实施顺序快照，2026-05-17）

当前 active REQ 是 v0.1.1 的目标队列。它先收敛到 FrameGraph v1、Directional Shadow、CSM，再补教程支撑和架构概念文档；HDR/Post、PBR 完整管线、G-Buffer/Deferred、Task-based 并行、Web Editor、Engine CLI/MCP、AssetRegistry 热重载均不在本轮 active 队列内。

| REQ | 主题 | 实施窗口 |
|---|---|---|
| `REQ-042-a` | FrameGraph v1 resource / target / pass execution | v0.1.1 多 pass 共同前置，未开始 |
| `REQ-042-b` | Directional shadow map 与 depth-only pass | 第一个真实 multiple pass，未开始 |
| `REQ-042-c` | Cascaded Shadow Maps | 近期渲染能力截止点，未开始 |
| `REQ-043-a` | Shadow 阶段教程支撑 | 完成 CSM 后补教程所需能力，未开始 |
| `REQ-043-b` | 架构概念文档展开与 Mermaid 图 | 完成上面能力后解释系统模块归属，未开始 |

## 当前 pending REQ（后续候选，2026-05-17）

以下需求已从 active 队列移入 `pending/`，避免 v0.1.1 开发同时引入过多方向：

| REQ | 主题 | 后置原因 |
|---|---|---|
| `REQ-042-a` | 光源资产与自定义光源注册入口 | 教程扩展 API，不是 shadow/CSM 前置 |
| `REQ-042-b` | Editor toolbar 与 command 扩展注册入口 | 教程扩展 API，不是 shadow/CSM 前置 |
| `REQ-042-c` | 自定义场景节点类型注册入口 | 教程扩展 API，不是 shadow/CSM 前置 |
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
- `finished/`：归档需求（按时间快照不重排）
- `notes/roadmaps/`：跨 REQ 的优先路径与阶段编排
