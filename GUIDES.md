# 开发者指南

本仓库使用一套结构化的流程来管理"从想法 → 需求 → 实施 → 归档 → 文档"的全周期。所有关键步骤都有对应的 Claude Code slash 命令，位于 `.claude/commands/` 下，直接在会话里输入 `/命令名` 即可调用。

本文件说明这些命令的职责、调用顺序、以及典型使用场景。

---

## 目录结构与产出物

| 路径 | 内容 | 谁负责写 |
|------|------|---------|
| `docs/requirements/*.md` | 活动中的需求文档（尚未完成代码实施） | `/draft-req` 创建 |
| `docs/requirements/finished/*.md` | 已落地 + 已校验的需求归档 | `/finish-req` 移入 |
| `openspec/changes/<name>/` | 活动中的 openspec change（proposal / design / specs / tasks） | `/opsx:propose` 创建 |
| `openspec/changes/archive/YYYY-MM-DD-<name>/` | 已归档的 openspec change | `/opsx:archive` 移入 |
| `openspec/specs/<capability>/spec.md` | 主 spec，是当前代码的权威契约 | `/opsx:archive` 同步 |
| `notes/` | 面向人类阅读者的中文项目速览 | `/update-notes` 生成与增量更新 |
| `docs/design/*.md` | 深度技术设计文档 | 人工编写，`/sync-design-docs` 维护索引 |

---

## 典型流程

```
┌────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐  ┌───────────────┐  ┌──────────────────┐
│ /draft-req │→ │/opsx:propose│→ │ /opsx:apply │→ │/opsx:archive│→ │/finish-req │→ │ /update-notes │→ │ /commit-changes  │
└────────────┘  └─────────────┘  └─────────────┘  └─────────────┘  └────────────┘  └───────────────┘  └──────────────────┘
  讨论 + 写       生成 change      实施代码         归档 +           校验 +           同步 notes        stage + commit
  需求文档         四件套          + 跑测试         同步主 spec       归档需求          摘要              到 git
```

每一步都是可中断的检查点。只要当前步骤产出对齐预期，可以停下换别的工作；稍后继续时命令会从当前状态接着走。

> `/commit-changes` 是所有其他命令的通用收尾 — 其他命令只写文件，不碰 git。一次完整循环结束后用本命令把文件系统上的改动同步到 git 历史。

---

## 命令详解

### `/draft-req` — 讨论并形成需求文档

**输入**: 可选一句话 brief，如 `/draft-req "支持 instanced rendering"`；无参数则完全交互。

**做什么**: 通过 6 个 phase 和开发者对话，把一个模糊想法打磨成结构化的 `docs/requirements/<NNN>-<title>.md`。

**Phase 流程**:

1. **扫描现有需求库** — 读全部 `docs/requirements/` + `finished/`，建立编号与状态清单
2. **Phase 1 动机** — 问当前痛点 / 触发时机 / 失败模式
3. **Phase 2 代码验证** — 主动 Grep 定位用户描述的问题在源码里的真实位置，行号必须从 Grep 结果拿，描述和代码脱节时会停下反问
4. **Phase 3 目标状态** — 成功标准 / 不变的东西 / API 变化范围
5. **Phase 4 分解为 R1..Rn** — 起草初版，和你迭代到认可
6. **Phase 5 边界 + 依赖 + 下游** — 明确不做 / 上游依赖 / 解锁下游
7. **Phase 6 冲突扫描** — 扫描 finished / 活动需求 / openspec changes，重叠时报告 + 建议调整
8. **编号 + 文件名** — 规则 `max(NNN) + 1`，确认后允许 `008a/b/c` 变体
9. **按模板起草** — 对齐 `finished/*.md` 的段落结构
10. **预览 + yes/edit/redo** — 写盘前确认
11. **写入 + 总结** — 落地后指向 `/opsx:propose` 或 `/finish-req`

**护栏**:
- 讨论价值在前半段，不接受跳过 Phase 1-5
- 代码引用 `src/... :LINE` 必须 Grep 确认过
- 只写 `docs/requirements/` 顶层，不写 `finished/`
- 不改动已有需求文件（冲突时只报告）
- 一次会话只写一份需求
- 不触发代码修改

**典型场景**:
- 新功能想法成熟到可写需求 → `/draft-req "新功能描述"`
- 重构想法还在雏形 → `/draft-req`
- 发现应该拆出子需求 → 在原需求里讨论然后 `/draft-req`
- 代码实施过程发现漏了一环 → `/draft-req "REQ-XXX 补充"`

---

### `/opsx:propose` — 把需求转成 openspec change

**输入**: change 名（kebab-case）或需求文件路径。

**做什么**: 在 `openspec/changes/<name>/` 下创建一个 change 目录，并顺序生成 4 份文件：

1. **`proposal.md`** — Why（问题陈述）+ What changes（改动清单）+ Capabilities（涉及哪些 spec）+ Impact
2. **`design.md`** — 关键技术决策（每个决策要有替代方案对比）+ 风险与权衡 + 迁移计划
3. **`specs/<capability>/spec.md`** — spec delta，用 `## ADDED Requirements` / `## MODIFIED Requirements` / `## REMOVED Requirements` 头部组织，每个 requirement 至少一个 `#### Scenario`
4. **`tasks.md`** — `- [ ] X.Y 任务描述` 格式的可打勾清单

**护栏**:
- 每一份产物写完后都会 `openspec validate`
- `specs/` 里的 delta 要对应 proposal 里声明的 Capabilities
- 不会跳步：proposal 没写完不能写 design；design + specs 没写完不能写 tasks

**典型场景**:
- 跟完一次 `/draft-req` → 立即 `/opsx:propose <needed-change-name>` 启动实施准备

---

### `/opsx:apply` — 按 tasks.md 实施代码改动

**输入**: change 名（可从上下文推断）。

**做什么**: 读取 `tasks.md`，按顺序执行每个未打勾的任务。每完成一个就把 `- [ ]` 改成 `- [x]`，并在必要时运行 `cmake --build build` 确认没引入编译错误。

**工作模式**:
- 一次一个任务，写最少的代码改动
- 任务间有依赖时严格按顺序推进
- 遇到歧义或设计问题 → 停下问用户或建议更新 `design.md` / `specs/`
- 遇到构建错误 → 立即修复（不跳到下一个任务）
- 遇到需要更大范围改动 → 停下，询问是否调整 scope

**护栏**:
- 不会在任务不明确时硬猜
- 不会跳过测试
- 发现 spec 有问题时可以建议修改 spec，但不会偷偷改
- 所有改动保持最小作用域

**典型场景**:
- `/opsx:propose` 生成完 tasks 后直接 `/opsx:apply`
- 中途被中断，下一次调用 `/opsx:apply` 会从第一个未打勾的任务继续

---

### `/opsx:archive` — 归档 change + 同步到主 spec

**输入**: change 名。

**做什么**:

1. 检查 4 份产物是否 done，tasks.md 是否全部打勾
2. 扫描 `openspec/changes/<name>/specs/` 里的 delta spec，对比 `openspec/specs/<capability>/spec.md` 的主 spec
3. 展示变更摘要（ADDED / MODIFIED / REMOVED / RENAMED 数量），让开发者确认
4. 合并 delta 到主 spec
5. `mv openspec/changes/<name> openspec/changes/archive/YYYY-MM-DD-<name>/`
6. 报告归档位置

**护栏**:
- 有未打勾的 tasks → 警告 + 询问是否仍要归档
- 主 spec 路径不存在（新能力）→ 直接把 delta 作为主 spec 写入
- 同一天同名归档冲突 → 停下报错
- 不会强制覆盖已有归档

**典型场景**:
- `/opsx:apply` 跑完所有任务 → 立即 `/opsx:archive`

---

### `/finish-req` — 校验需求文档 + 归档到 finished/

**输入**: 需求文件路径 / 编号前缀，如 `/finish-req 005` 或 `/finish-req 005-unified-material-system.md`。

**做什么**:

1. 解析需求文件，识别每个 `Rn` 的具体宣称
2. 用 Grep/Read 逐项在代码里验证，分类为：`✓ Implemented` / `⚠ Drift` / `✗ Missing` / `⊘ Superseded`
3. 展示验证表格给开发者
4. 看机会扫描简化点（dead code / 重复逻辑 / 过度抽象）
5. 修复 Drift / Missing + 应用确认的简化（每 2-3 次编辑跑一次构建）
6. 运行相关的测试
7. 更新需求文档的"实施状态"段
8. `mv docs/requirements/<file>.md docs/requirements/finished/`

**与 `/opsx:archive` 的区别**:
- `/opsx:archive` 归档的是 **openspec change**（proposal/design/tasks 四件套）
- `/finish-req` 归档的是 **需求文档**（`docs/requirements/*.md`）
- 这两个系统是**并行**的跟踪机制——需求文档是给人看的高层意图，openspec 是给实施流程用的精确契约。理想情况下一个代码改动会同时穿过两套系统：`/draft-req` → `/opsx:propose` → `/opsx:apply` → `/opsx:archive`（spec 已更新）→ `/finish-req`（需求文档归档）

**护栏**:
- 构建不绿不允许归档
- 发现严重 drift 时会询问是否要走 `/opsx:propose` 重新实施
- 尊重 `Superseded by` 横幅，不做重复验证
- 不会因为"简化"删测试
- 不跨需求扩大作用域

**典型场景**:
- REQ-005 的 openspec change 已 archive，代码已经绿 → `/finish-req 005`
- 旧需求重新验证 → `/finish-req 003b`
- 某个 Superseded 的需求需要归档整理 → `/finish-req` 检测到 banner 后直接询问是否仅移动文件

---

### `/update-notes` — 生成或增量更新 notes/

**输入**:
- `/update-notes` — 增量模式（默认）
- `/update-notes --full` — 强制全量重写
- `/update-notes --dry-run` — 只报告会变更的文件
- `/update-notes <subsystem>` — 强制刷新某个子系统文档

**做什么**:

**首次运行**:
- 扫描 `AGENTS.md` / `CLAUDE.md` / `openspec/specs/` / `docs/design/` / `src/` 目录结构
- 生成完整 notes 树：`README.md` / `architecture.md` / `glossary.md` / `subsystems/<name>.md` × N
- 写入 `notes/.sync-meta.json` 记录本次 commit

**增量运行**:
- 读 `.sync-meta.json` 拿 `lastSyncedCommit`
- `git log ${lastSyncedCommit}..HEAD --name-only` 获取变更文件
- 按内置映射表（`src/core/resources/material.*` → `material-system.md` 等 13 条）投射到受影响的 notes 文件
- **仅 `openspec/changes/archive/**` 触发更新**——在途 change 被排除
- 先展示计划再写盘

**notes/ 的定位**: 
- 面向第一次看项目的人
- 是摘要与导航，**不是** spec 的克隆
- 每个子系统页链回权威 spec + design doc
- 中文写作，英文符号

**护栏**:
- 不删除 notes 下的任何文件（废弃时加横幅而非 rm）
- 保护 `<!-- manual -->` / `<!-- manual:end -->` 之间的手写内容
- 代码引用必须带真实行号
- 跨分支失效（`lastSyncedCommit` 被 gc/rebase）时停下问用户，不静默降级

**典型场景**:
- 首次上手项目 → `/update-notes --full`
- 每次 `/opsx:archive` 后 → `/update-notes` 让摘要跟上最新代码
- 想单独刷新某一页 → `/update-notes material-system`
- 想先预览再决定 → `/update-notes --dry-run`

---

### `/commit-changes` — 总结 diff + 审查 + 执行 commit

**输入**:

- `/commit-changes` — 自动起草 Conventional Commits 风格的消息 + 调用子代理审查
- `/commit-changes "feat: custom message"` — 使用用户提供的消息（仍会跑审查 + 需要确认 stage 列表）
- `/commit-changes --skip-review` — 跳过子代理审查（仅限 docs / notes / 极小改动，默认不跳）

**做什么**:

1. **收集状态** — `git status` + `git diff` + `git diff --staged` + `git log -n 10`（并行跑）
2. **敏感文件扫描** — 正则匹配 `.env` / `*credentials*` / `*.pem` / `*.key` / `*secret*` / `*.token` / `*.pfx` / `*.p12` / `id_rsa*`；diff 内容扫 `BEGIN PRIVATE` / `api_key=` / `Bearer ey` 等；命中立刻停下
3. **按主题分组变更** — 文档 / 测试 / core / infra / backend / shader / 构建 / Claude 配置 / openspec / notes / 其他；涉及 3+ 无关分组时停下建议拆分
4. **起草消息** — 按 type 推断规则选 `feat/fix/refactor/docs/test/chore/perf/ci` 之一；description 祈使语气 + ≤50 字符 + 不加句号；必要时写 body 展开多个改动点；参考 `git log` 保持与历史语言风格一致
5. **调用 `pre-commit-reviewer` 子代理** — 通过 Agent 工具启动一个独立审查员，传入 message draft + 主题分组摘要；子代理独立跑 `git diff` + 读代码 + 按分级规则生成报告；返回 **PROCEED / PROCEED_WITH_CAUTION / BLOCK / SPLIT** 之一
6. **展示计划 + 审查报告** — 表格形式展示 type + message + stage 文件 + 审查报告；根据推荐状态调整默认答案（BLOCK 时默认 cancel，必须 `yes anyway` 才覆盖）
7. **确认 yes/edit message/edit files/cancel** — **永远**需要用户点头；`edit files` 会重新跑审查（范围变了）
8. **Stage** — 只用 `git add <显式路径>`，禁止 `-A` / `.` / `-u`
9. **Commit** — 用 heredoc 传递消息保持多行格式；禁止 `--amend` / `--no-verify` / `--no-gpg-sign`
10. **验证** — `git status` + `git log -n 1`；报告 hash + 消息 + 工作区状态

**Pre-commit hook 失败时**:

- **禁止** `--amend` 或 `--no-verify`
- 修复根因（format / lint / test），重新 stage 被 hook 自动修改的文件
- **创建 NEW commit**，不要 amend — commit 失败意味着之前的 commit 从未产生，amend 会改到**上一个已存在的** commit，是错的

**护栏**:

- 永远让用户确认一次，不能跳过
- 只 stage 显式列出的文件
- 敏感文件拦截不可关闭
- 不自动 push，最后一步是 `git log`
- 禁止 amend / skip hooks / force push / 改 git config
- 单 commit 原则上单主题，跨 3+ 分组时必须提示拆分
- 拒绝空话消息：`update` / `wip` / `fix` / `misc fixes` 等不接受

**典型场景**:

- 一个完整 session 改完后收尾 → `/commit-changes`
- 改动跨多个主题，想分多次提交 → 反复调用，每次用 `edit files` 只 stage 一个主题的文件子集
- 自己已经想好消息了 → `/commit-changes "fix: specific description"`
- `/opsx:archive` / `/finish-req` / `/update-notes` 都写完了 → `/commit-changes` 一次把它们的产物一起提交

---

### `/sync-design-docs` — 同步 AGENTS.md 的设计文档索引

**输入**: 无参数。

**做什么**:
- 对比 `AGENTS.md` 的 Design Documents 小节和 `docs/design/` 目录实际文件
- 缺失项从源码 / 现有 spec 生成
- 已有项检查摘要是否过时

**与 `/update-notes` 的区别**:
- `/sync-design-docs` 维护的是 **AGENTS.md 的索引** + **docs/design/ 的深度文档**
- `/update-notes` 维护的是 **notes/ 的项目速览**
- 前者是权威设计文档，后者是面向新人的摘要

---

## 快速参考表

| 阶段 | 你想做什么 | 用哪个命令 |
|------|-----------|-----------|
| 0 | 有个改动想法，还没写需求 | `/draft-req` |
| 1 | 需求文档写好了，要开始准备实施 | `/opsx:propose` |
| 2 | 四件套都齐了，开始写代码 | `/opsx:apply` |
| 3 | 代码写完了，要归档 change + 同步 spec | `/opsx:archive` |
| 4 | spec 已同步，要把需求文档也归档 | `/finish-req` |
| 5 | 需求归档了，想让 notes 跟上最新代码 | `/update-notes` |
| 6 | 所有文档都写完了，要把改动提交到 git | `/commit-changes` |
| - | 只想刷新 notes 里某一页 | `/update-notes <子系统名>` |
| - | 只想看 notes 会改什么但不落地 | `/update-notes --dry-run` |
| - | 只想维护 AGENTS.md 的设计索引 | `/sync-design-docs` |
| - | 只想校验某个归档需求代码没回退 | `/finish-req <编号>` |
| - | 只想提交已有的 staged 改动 | `/commit-changes` |
| - | 想分多次提交（跨主题拆分） | 反复调用 `/commit-changes`，用 `edit files` 选子集 |

---

## 常见问题

### 能跳过 `/draft-req` 直接 `/opsx:propose` 吗？

可以。`/opsx:propose` 本身能接受一句话 brief 或者一份现成的需求文档作为输入，自己起草四件套。选哪条路径取决于你对改动的理解深度：
- **理解已经清晰** → 直接 `/opsx:propose`
- **想先理清思路** → `/draft-req` 先形成需求，再 `/opsx:propose`

### 需求文档和 openspec spec 是不是同一个东西？

不是。它们服务于不同目的：

| | 需求文档 (`docs/requirements/`) | openspec spec (`openspec/specs/`) |
|---|--------------------------------|-----------------------------------|
| 读者 | 开发者（项目背景 + 决策脉络） | 实施者（精确契约） |
| 风格 | 自由叙述 + Rn 分解 | `Requirement / Scenario` 强结构 |
| 生命周期 | `draft-req → finish-req` | `opsx:propose → opsx:archive` |
| 数量 | 一次改动一份 | 一次改动可能 MODIFY 多份 spec |

理想情况下一个改动穿过两套系统，两者相互印证。

### 如果 `/opsx:apply` 中途发现 design 有问题怎么办？

停下 `/opsx:apply`，**不要**强行实施一个你知道有问题的设计。用常规 Edit 工具修改 `design.md` / `specs/<capability>/spec.md`，必要时也修改 `tasks.md`，然后重新 `/opsx:apply`。命令会从第一个未完成任务继续。

### 归档后发现漏了一个任务怎么办？

两种选择：
- **小问题** → 用 `/draft-req` 写一个补丁需求（例如 `005a`），走完整流程
- **大问题** → 直接 `/opsx:propose` 一个新 change 重新修复

不要去改已归档的 `archive/` 目录或者 `finished/` 里的需求 — 归档是历史快照。

### 命令之间的状态是怎么传递的？

每个命令都通过**文件系统**传递状态，而不是内存。具体：

- `/opsx:propose` 的输出 → `openspec/changes/<name>/` 目录
- `/opsx:apply` 读 `tasks.md`，写 `src/`，更新 `tasks.md` 的打勾
- `/opsx:archive` 读 change 目录，写 `openspec/specs/`，移动目录到 `archive/`
- `/update-notes` 读 `.sync-meta.json` 和 `git log`，写 `notes/`
- `/finish-req` 读 `docs/requirements/`，写 `src/`，移动文件到 `finished/`
- `/commit-changes` 读 `git status` + `git diff`，调用 `git add` + `git commit`（唯一一个会动 git 历史的命令）

这意味着你可以在任何命令之间停下，用常规工具（编辑器 / `git status` / `ls`）检查状态，甚至手动修正，然后再继续。

### `/commit-changes` 会自动 push 吗？

**不会**。本命令只做 `git add` + `git commit`，最后一步是 `git log -n 1` 确认 commit 已落地。是否 push、push 到哪个分支、是否 rebase / force push，都由人工决定。这条边界刻意保持——让 agent 自动 push 的风险收益比完全不划算。

### 提交消息可以用中文吗？

可以。命令会读 `git log -n 10` 作为风格参考，自动跟随仓库里现有提交的语言偏好。一个 commit 内保持一致（description 英文 + body 中文，或者全中文，都不接受）。

### `/commit-changes` 会把未追踪的 `build/` 目录提交进去吗？

不会。`.gitignore` 已经屏蔽 `build/`，命令不使用 `git add -A`，只 stage 步骤 5 里用户确认过的具体文件。另外 step 2 的敏感文件扫描会再兜底一次。

### 我已经手动 `git add` 了一些文件，命令会怎么处理？

命令会识别已 staged 的子集，在计划展示时列出来（`Already staged` 段）。如果你只想提交这些已 staged 的文件，可以在 `edit files` 模式下取消所有"要 stage 的新文件"。

---

## 子代理（Subagents）

命令是**单进程**的工作流；有些场景需要在命令执行中途启动一个**独立的、上下文隔离的审查员或执行者**——这就是子代理（subagent）。子代理定义在 `.claude/agents/*.md`，被命令通过 Agent 工具按名字调用。

### 本项目的子代理

| 子代理 | 作用 | 被谁调用 |
|-------|------|---------|
| `pre-commit-reviewer` | 审查待提交的 diff，按 CRITICAL / HIGH / MEDIUM / LOW 分级给出发现 + 推荐 PROCEED/BLOCK/SPLIT | `/commit-changes` 在 step 5 自动调用 |

### `pre-commit-reviewer`

**位置**: `.claude/agents/pre-commit-reviewer.md`
**工具**: `Read` / `Grep` / `Glob` / `Bash`（只读，**禁止**写文件）

**做什么**:

1. 独立跑 `git diff` / `git diff --staged` / `git status`（不信任调用方给的版本）
2. 对每个改动文件 Read 周围 30-50 行，理解改动**做了什么**而不是只看字节
3. 按分级规则走一遍：
   - **CRITICAL**: 硬编码密钥 / 注入 / 空指针 / 破坏公开 API
   - **HIGH**: 深嵌套 / 未同步 mutation / 吞掉异常 / 性能回退 / 新公开 API 无测试
   - **MEDIUM**: 过期注释 / 风格不一致 / 魔法数字 / 缺 const/noexcept / 死代码
   - **LOW**: 个人偏好 / 小简化
4. 检查**项目专属规则**：裸指针禁令、构造注入、layer 依赖（core → infra → backend）、`LX_core::backend` 命名空间、pipeline identity 通过 `GlobalStringTable::compose` 而非 `getPipelineHash()`（REQ-007 之后）
5. 简化机会扫描：单 caller 的 helper、只有一个实现的 template、死代码
6. 测试覆盖检查：`src/core/**` / `src/infra/**` 新增逻辑必须有对应测试
7. 文档与 spec 漂移：动了 `openspec/specs/**` 但没对应的 `openspec/changes/**` → HIGH；动了 `docs/requirements/finished/**` → HIGH
8. 返回结构化报告 + 四种推荐状态之一

**推荐状态**:

| 状态 | 含义 | `/commit-changes` 的默认答案 |
|------|------|--------------------------|
| `PROCEED` | 无 CRITICAL 无 HIGH，LGTM | `yes` |
| `PROCEED_WITH_CAUTION` | 只有 MEDIUM/LOW，合并决策在开发者 | `yes`（但显式提醒） |
| `BLOCK` | 有 CRITICAL 或 HIGH，建议修完再提交 | `cancel`（需 `yes anyway` 覆盖） |
| `SPLIT` | diff 覆盖了不相关主题，建议拆分 | `edit files` |

**护栏**:

- **只读**：子代理的工具集不包含 Edit / Write，结构上无法修改文件
- **findings 可执行**：每条 finding 必须带 `文件:行号` + 具体修复建议，不接受"建议重构"这种空话
- **不审查生成文件**：build artifacts、`.sync-meta.json` 等跳过
- **总量封顶 10 条**：按严重级别排序，多余的标注省略
- **沉默优于挑刺**：干净 diff 应该得到 3 行 PROCEED 报告，不是 10 条风格偏好
- **尊重上下文**：WIP 文档不要强求测试；重构不要因为"没有新功能测试"报 HIGH
- **尊重 Superseded 标记**：带 `Superseded by REQ-X` 的代码是过渡期，不按新 REQ 的标准报错

### 为什么不让 `/commit-changes` 自己审查？

这个设计有意分离：

- **命令**: 有状态，记得用户在 session 里说过什么，看过什么文件
- **子代理**: 上下文隔离，从零开始看 diff，没有"这段代码我刚写完所以没问题"的偏见

把审查拆成独立进程，能获得"外部视角"——这是夹带偏见的同一个 session 做不到的。子代理看到的 diff 和你写的时候是同一份，但它**不知道**你是什么意图，必须从代码本身推断，因此更容易发现"代码说的和注释/commit message 说的不一致"这种问题。

### 未来可能的子代理

这套模式可以扩展到其他场景。候选：

- `spec-drift-detector` — 扫描 `openspec/specs/` 和 `src/` 的一致性
- `test-coverage-analyzer` — 深度扫描新代码的测试缺口
- `architecture-reviewer` — 专审 layer 依赖 / 命名空间 / 循环引用
- `perf-regression-scanner` — 对比 benchmark 结果

目前只实现了 `pre-commit-reviewer`；其他按需补充。

---

## 延伸阅读

- `.claude/commands/*.md` — 每个命令的完整说明
- `.claude/agents/*.md` — 每个子代理的完整说明
- `AGENTS.md` — 项目架构总览
- `CLAUDE.md` — 快速索引 + Specs Index + Design Docs Index
- `openspec/specs/` — 所有能力的权威 spec
- `docs/design/` — 深度设计文档
- `docs/requirements/finished/` — 历史需求归档，看项目是如何演化到当前状态的
