# 需求文档目录约定

本目录存放**进行中**的需求文档（in-flight REQs）。已落地需求归档到 `finished/`。自动生成的导航索引在 `index.md`（由 `scripts/notes/generate_site_config.py` 维护，不要手编）。

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
   - **不**改 `openspec/changes/archive/` 中的归档（历史快照不动）

7. **`finished/` 归档不动**：已归档需求的文件号是当时完成顺序的快照，**不**回溯重排。新增 finished 时直接保留它在 active 时的文件号。

8. **speculative 候选编号用字母**：研究文档中的“未来 REQ”用字母占位（`REQ-A`、`REQ-B` ...），不要占用数字号。数字号留给真正落地到 `notes/requirements/` 的文件。

## 当前 pending REQ（实施顺序快照，2026-05-11）

本轮按代码事实重整后，active REQ 不再围绕“编辑器 chrome / 组件成熟度 / DebugDraw v2”展开，而是收敛到一个更明确的目标：**让 `scene_viewer` 可以快速搭建并保存测试场景文件**。

| 文件号 | 主题 | 说明 |
|---|---|---|
| [041-d](041-d-scene-authoring-toolbar-palette.md) | 工具栏几何体 / 光源 / 相机拖拽创建 | 建立最快的场景创建入口 |
| [041-e](041-e-scene-authoring-inspector-material-and-visibility.md) | Inspector 的材质 / 颜色 / 可见性收敛 | 让创建后的对象能直接在面板里细调并保存 |
| [041-f](041-f-scene-authoring-node-rename-duplicate.md) | Rename / Duplicate 对齐 scene document | 提升批量搭测试场景的编辑效率 |
| [041-g](041-g-rtr-light-experiment-foundation.md) | RTR 第五章多类型光源实验底座 | 为 Directional / Point / Spot Light 建立可保存、可编辑、可绑定的数据入口 |
| [041-h](041-h-rtr-material-experiment-foundation.md) | RTR 第五章实验材质接入底座 | 让新增实验材质能快速挂到场景节点并保存节点级参数覆盖 |

## 本轮删除的 active REQ（2026-05-11）

以下文档已从 active 队列删除，因为它们与当前“快速搭测试场景文件”的目标不再同向，或者建立在与现状不一致的架构假设上：

- `041-g-component-v2-multi-and-enable.md`
- `041-h-mesh-level-triangle-picking.md`
- `041-i-debug-draw-persistent-and-mesh.md`
- `041-j-component-dependency-declaration.md`

另外，旧的 `041-d` / `041-f` 内容已改写并重命名为新的 scene authoring 需求，原“undo/redo UI / 菜单栏与主题”不再保留为 active REQ。

## 历史

- 2026-05-11：按当前代码与目标重整 active REQ。删除 `041-g`~`041-j`，并把保留需求改写为面向测试场景搭建的 `041-d` / `041-e` / `041-f`。
- 2026-05-11：在 `041-f` 后新增 RTR 第五章实验底座需求：`041-g` 多类型光源数据与作者入口、`041-h` 实验材质接入与节点级参数覆盖。两者只定义环境能力，不实现具体光照公式或 Gooch shader。
- 2026-05-08：`REQ-041-c` 编辑器多选 / 框选完成验证并归档到 `finished/041-c-editor-multi-select.md`。
- 2026-05-07：`REQ-040-a` Editor 命令总线完成验证并归档到 `finished/040-a-editor-command-bus.md`，pending 队列从 `REQ-041-a` 开始。
- 2026-05-07：`REQ-039-a` DebugDraw 子系统完成验证并归档到 `finished/039-a-debug-draw-subsystem.md`。
- 2026-05-06：`REQ-041-a` ImGui Editor MVP、`REQ-041-b` 命令总线 v2 相关前置逐步稳定，随后开始进入 v2 需求拆分期。
- 2026-05-01：建立“REQ 文件号 = 实施顺序”约定。

## 相关

- `index.md`：自动生成的 pending REQ 列表（导航用）
- `finished/`：归档需求（按时间快照不重排）
- `notes/roadmaps/`：跨 REQ 的优先路径与阶段编排
