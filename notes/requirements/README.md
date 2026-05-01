# 需求文档目录约定

本目录存放**进行中**的需求文档（in-flight REQs）。已落地需求归档到 `finished/`。自动生成的导航索引在 `index.md`（由 `scripts/notes/generate_site_config.py` 维护，不要手编）。

> 自动生成的 `index.md` 与本手写 `README.md` 各司其职：前者列出所有进行中的 REQ 文件名 + 标题，后者解释**约定**与提供**实施顺序快照**。

## 编号约定（自 2026-05-01 起）

**REQ 文件号 = 实施顺序**。读 REQ 列表时直接按文件号升序就是预期实施顺序。

具体规则：

1. **文件号即实施顺序**：`034 < 035 < 036 ...` 表示前者应早于后者实施。打开目录看到 `034-foo.md` / `035-bar.md` / `036-baz.md`，意思就是 foo 先做、bar 次之、baz 最后。

2. **一个 REQ 文件 = 一个连续实施周期**：同一文件不要同时包含"现在就做"和"等另一个新需求之后再做"的两段工作。若旧 REQ 横跨多个实施窗口，先拆文件，再排编号。

3. **新 REQ 取号位置**：起草新 REQ 时，先在大脑里把它跟现有 pending REQ 排个实施顺序：
   - **预期最后实施** → 取下一个连号（`max(existing) + 1`）
   - **预期插入某个 pending REQ X 的同一实施槽** → 优先使用后缀族，不整体后移后续编号。例如把 `020-foo.md` 拆成 `020-a-foo.md`、`020-b-bar.md`，标题对应 `REQ-020-a`、`REQ-020-b`
   - **预期排在某个 pending REQ X 之前，且不是 X 的拆分 / 补充** → 只有确认这是全局顺序变化时，才给 X 之后的 pending REQ 编号 +1
   - **预期跟某 pending REQ Y 大致同步** → 取 Y 后面一个连号
   - **预期只让旧需求的一部分后置** → 先拆旧需求，再用同一数字前缀的后缀表达顺序

4. **拆分旧需求**：如果旧需求横跨多个实施周期，把已经可以先做的 R 项留在 `NNN-a`，把后置 R 项移到 `NNN-b` / `NNN-c` 等新的 active REQ。新旧文档都写一段简短说明，记录拆分来源、拆分日期、保留范围和移出范围。历史上已经存在的 `NNNa` 文件不强制迁移；新文件统一使用 `NNN-a-*.md`。

5. **后缀顺序**：同一数字槽内按 `020-a < 020-b < 020-c < 021` 排序。不要让 active 目录里同时存在未拆分的 `020-foo.md` 和同槽的 `020-a-*`；一旦拆分，原 `020` 也改成 `020-a`。

6. **实施顺序变化时的重排**：当依赖图变了（新依赖、新优先级），如果导致现有 pending 文件号顺序与实施顺序不一致，先判断能否用局部后缀族表达。只有不能局部表达时才**重排数字号**：
   - 调整需要的 `mv` 命令：`mv NNN-slug.md MMM-slug.md`
   - 同步更新所有引用：`grep -rln "NNN-slug" notes/ | xargs sed -i 's/NNN-slug/MMM-slug/g'`
   - 同样替换文件内的 `# REQ-NNN:` / `# REQ-NNN-a:` 标题与跨 REQ 引用文本 `REQ-NNN` / `REQ-NNN-a`
   - 同步更新 open `openspec/changes/` 中的引用
   - **不**改 `openspec/changes/archive/` 中的归档（历史快照不动）
   - 提交前 `grep -rn "REQ-NNN\|REQ-NNN-a\|NNN-slug\|NNN-a-slug" notes/` 确认零残留

7. **`finished/` 归档不动**：已归档需求的文件号是当时完成顺序的快照，**不**回溯重排。新增 finished 时直接保留它在 active 时的文件号。

8. **speculative 候选编号用字母**：研究文档（`notes/roadmaps/research/*`）中预想的"未来 REQ"用字母占位（`REQ-A`、`REQ-B` ...），不要占用数字号。数字号留给真正落地到 `notes/requirements/` 的文件。

## 为什么这么做

普通项目（GitHub Issues / RFC / Linux 内核等）用创建顺序编号，理由是"编号是不可变身份标识"。本仓库选择 *实施顺序编号* 是因为：

- LX 是一个**长周期单作者推进**的引擎项目，需求队列通常 < 20 条，重排成本可控
- 实施顺序的可视化对 *作者的下一步是什么* 这个高频问题最有价值
- 大多数 REQ 引用是 **路径链接**（`[REQ-NNN](path/NNN-slug.md)` / `[REQ-NNN-a](path/NNN-a-slug.md)`），用 sed 批量更新成本低
- 重排发生频率低（每次大型规划重排一次，~月级）
- 后缀族用于吸收局部拆分，避免一次需求讨论扩展就牵动后续所有编号

## 当前 pending REQ（实施顺序快照，2026-05-01）

| 文件号 | 主题 | 时机 | 说明 |
|--------|------|------|------|
| [037-a](037-a-component-model-foundation.md) | IComponent 基础设施 + Mesh / Material / Skeleton 转 component | Phase 1.5 第 3a 步 | 原 REQ-037 拆分出的 component 模型 |
| [037-b](037-b-camera-as-component.md) | Camera 作为 component 接入 SceneNode | Phase 1.5 第 3b 步 | 原 REQ-037 拆分出的 Camera 接入 |
| [038](038-ray-aabb-picking-min.md) | ray-AABB picking 暴力版 | Phase 1.5 第 4 步 | AABB + ray slab |
| [039](039-debug-draw-subsystem.md) | DebugDraw 子系统 | Phase 1.5 第 5 步 | drawLine/wireSphere/frustum/cone/arrow/axis |
| [040](040-editor-command-bus.md) | Editor 命令总线 | Phase 1.5 第 6 步 | 文本协议 + 控制台 |
| [041](041-imgui-editor-mvp.md) | ImGui Editor MVP | Phase 1.5 第 7 步 | ImGuizmo + 4 面板 |
| [042](042-render-target-desc-and-target.md) | RenderTarget 拆 desc + binding | Phase 1.5 完工后 / Phase 1 REQ-103 之前 | 跨 pass 资源前置 |

后续 Phase 1 渲染深度（多光源 / Shadow / PBR / IBL / G-Buffer）的 REQ 取号将从 043 起，按重排后的优先路径排号。

## 历史

- 2026-05-01：补充 `NNN-a` / `NNN-b` 后缀族约定。需求讨论扩展导致局部拆分时，优先把原 `020` 改成 `020-a`、新增 `020-b`，不再默认整体后移后续编号。
- 2026-05-01：原 REQ-037 (Camera 接入 SceneNode) 在评审中决定改用 component 模型方案，规模升级为架构级重构，拆为 `037-a-component-model-foundation.md`（IComponent 基础设施 + mesh/material/skeleton 转 component）+ `037-b-camera-as-component.md`（Camera 作为 component 接入），是上一条后缀族约定的首个应用。
- 2026-05-01：`REQ-034` 删除 `RenderTarget::getHash` dead code 已完成并归档到 `finished/034-remove-render-target-get-hash.md`，pending 队列从 `REQ-035` 开始。
- 2026-05-01：`REQ-036` 场景节点路径查询已完成并归档到 `finished/036-scene-node-path-lookup.md`，pending 队列从 `REQ-037-a` 开始。
- 2026-05-01：`REQ-035` Transform 组件已完成并归档到 `finished/035-transform-component.md`，pending 队列从 `REQ-036` 开始。
- 2026-05-01：建立"REQ 文件号 = 实施顺序"约定。同时把原 REQ-034 (RenderTarget 重写) 与原 REQ-042 (getHash cleanup) 编号 swap，让 cleanup 取最小号 034。研究文档 `frame-graph/` 中原 speculative "REQ-035..041" 改为字母 `REQ-A..G` 避免与实际落地的 035-041 (Phase 1.5) 冲突。

## 相关

- `index.md`：自动生成的 pending REQ 列表（导航用）
- `finished/`：归档需求（按时间快照不重排）
- `.codex/skills/draft-req/SKILL.md`：起草新 REQ 的工作流，对接本约定
- `notes/roadmaps/`：跨 REQ 的优先路径与阶段编排
