---
name: "Commit Changes"
description: Summarize git diff, draft a conventional commit message, and commit after confirmation
category: Git
tags: [git, commit, workflow]
---

分析当前工作区的 git 变更，起草符合 Conventional Commits 格式的提交消息，展示计划给用户确认，然后执行 `git add` + `git commit`。**永远要用户确认一次**，永远**不**自动 push。

**Input**: 可选一个消息覆盖 / flag

- `/commit-changes` — 完全自动起草消息，并调用 `pre-commit-reviewer` 子代理审查
- `/commit-changes "feat: custom message"` — 使用用户提供的消息（跳过起草步骤，仍需确认 stage 列表）
- `/commit-changes --skip-review` — 跳过代码审查（仅限 docs / notes / 极小改动；默认不跳）

**IMPORTANT**:

- **禁止** `git add -A` / `git add .` — 只 stage 具体文件
- **禁止** `--amend` / `--no-verify` / `--no-gpg-sign` / force push
- 检测到疑似敏感文件（`.env` / `*credentials*` / `*.pem` / `*.key` / `*secret*` / `*.token` / `*.pfx` / `*.p12` 等）时**拒绝 stage** 并警告
- 从不自动 `git push`，最后一步只跑 `git log` 确认

---

## Steps

### 1. 收集当前状态

并行运行（一个 Bash 消息里四个独立调用）：

```bash
git status
git diff
git diff --staged
git log -n 10 --oneline
```

- 若三种变更都为空（工作区干净）→ 报告 "No changes to commit" 并停止。
- 若只有 staged 变更 → 跳过 stage 步骤，直接到 step 5。
- `git log` 用来观察本仓库的提交消息风格（type 偏好、中英文、body 长度）。

### 2. 敏感文件扫描

对所有未提交文件名（working-tree + untracked + staged）做正则匹配：

```
\.env(\..+)?$
credentials
secret
\.(pem|key|pfx|p12|token)$
id_rsa
id_ed25519
```

命中任何一条 → **立刻停下**，报告受影响文件，让用户决定：

- 把文件加入 `.gitignore`
- 显式确认 "这不是敏感文件，继续 stage 它"
- 取消本次 commit

另外，对 `git diff` 的**内容**做一次快扫：搜索 `BEGIN RSA`, `BEGIN PRIVATE`, `api_key=`, `password=`, `Bearer ey` 等模式。命中同样停下。

### 3. 按主题分组变更

读 step 1 的输出，把所有受影响文件按主题聚类：

| 分组 | 匹配 |
|------|------|
| 文档 | `*.md`, `docs/**`, `AGENTS.md`, `CLAUDE.md`, `GUIDES.md`, `README*` |
| 测试 | `**/test*`, `**/*_test.*`, `src/test/**` |
| 源码 — core | `src/core/**` |
| 源码 — infra | `src/infra/**` |
| 源码 — backend | `src/backend/**` |
| Shader | `shaders/**` |
| 构建 | `CMakeLists.txt`, `cmake/**`, `scripts/**` |
| Claude 配置 | `.claude/**` |
| Openspec 流程产物 | `openspec/**` |
| Notes | `notes/**` |
| 其他 | 不匹配任何上面的 |

**检测范围跨度**：

- 只涉及 1-2 个分组 → 正常单 commit
- 涉及 3+ 个不相关分组（例如"src/core 重构 + 一份无关的 shader 修改 + CMake 调整"） → **停下报告**，建议拆成多个 commit 并给出具体拆分方案；询问用户：
  - `split` — 取消本次 commit，让用户重新选要 stage 的子集
  - `continue` — 接受混合主题作为单 commit

### 4. 起草提交消息

格式遵循本仓库的 git-workflow 约定：

```
<type>: <description>

<optional body>
```

**Type 推断**（按优先级从上到下）：

| 条件 | Type |
|------|------|
| 仅文档分组有变化 | `docs` |
| 仅测试分组有变化 | `test` |
| 源码分组主导且 diff 显示修正错误逻辑 / 加 guard / fix conditional | `fix` |
| 源码分组主导且是新类 / 新接口 / 新文件 | `feat` |
| 源码分组主导且是重写、改名、删除+替换、移动 | `refactor` |
| 仅构建 / Claude 配置 / openspec / notes | `chore` |
| 仅 CI 配置 | `ci` |
| 明确的性能优化（删除 O(n²) → O(n)、cache、batching） | `perf` |
| 多个分组混合 | 取主导分组的 type |

**Description 写作规则**：

- 一句话，祈使语气（"add X" / "fix Y" / "refactor Z"），**不加句号**
- ≤ 70 字符（建议 ≤ 50）
- 描述"是什么 + 为什么"而不是"改了哪些文件"
- 参考 step 1 的 `git log` 输出，保持与历史风格一致的语言（中文 / 英文）

**Body 规则**（可选）：

- 当改动跨 2+ 文件且需要解释关联时写一段 bullet list
- 每条 bullet 说**一个**具体改动点
- 需要时引用相关的 `REQ-NNN` / openspec change 名字
- 不重复 description 已经说过的话

**反面示例**（禁止）：

```
update
wip
fix bug
chore: misc changes
fix: various fixes
```

**正面示例**:

```
refactor: drive MaterialInstance UBO layout from shader reflection

- Replace hand-written BlinnPhongMaterialUBO struct with std140 byte buffer
- Add ShaderResourceBinding::members (REQ-004) as the layout source
- Remove DrawMaterial; MaterialInstance is now the sole IMaterial impl
```

### 4.5. 调用 pre-commit-reviewer 子代理

**默认执行**，除非用户传 `--skip-review`。

用 **Agent 工具** 启动一个子代理：

- `subagent_type`: `pre-commit-reviewer`
- `description`: "Pre-commit review of pending diff"
- `prompt`: 一段简短 brief，包含
  - 你起草的 commit message（或用户提供的）
  - Step 3 的主题分组摘要
  - 任何用户在会话里提到的重点（"我刚重写了 X" / "这是 WIP"）

子代理会：

- 独立跑 `git diff` / `git diff --staged` / `git status`
- 读取改动文件的上下文
- 按 `.claude/agents/pre-commit-reviewer.md` 里的分级规则生成一份结构化报告
- 返回 **PROCEED / PROCEED_WITH_CAUTION / BLOCK / SPLIT** 之一的推荐

**处理子代理返回**：

- `PROCEED` — 展示报告，step 5 的默认答案是 `yes`
- `PROCEED_WITH_CAUTION` — 展示报告，step 5 的默认答案是 `yes`，但显式提醒用户注意 MEDIUM findings
- `BLOCK` — 展示报告，step 5 的默认答案是 `cancel`；必须显式确认"我知道有 CRITICAL/HIGH 问题但仍要提交"才继续
- `SPLIT` — 展示报告 + 子代理给出的拆分建议，提示用户用 `edit files` 模式只选一个主题的子集

**子代理失败处理**：

- 若 Agent 调用本身报错 → 报告给用户，询问是降级到 `--skip-review` 还是放弃本次 commit
- 若子代理返回格式不规范 → 把原始输出直接打印给用户做人工审阅

### 5. 展示提交计划 + 审查报告

给用户完整的 commit 预览，**包含 step 4.5 返回的 review 报告**：

```
## Pre-commit 审查

<pre-commit-reviewer 子代理返回的完整报告，原样贴出>

Recommendation: PROCEED / PROCEED_WITH_CAUTION / BLOCK / SPLIT

---

## 提交计划

**Type**: <type>
**Files to stage** (<N>):
  + src/core/asset/material.hpp
  + src/core/asset/material.cpp
  + src/infra/material_loader/blinn_phong_material_loader.cpp
  ...

**Already staged** (<N>):
  ~ ...

**Untracked but SKIPPED** (<N>):
  ? build/...  (in .gitignore already — skip)
  ? foo.tmp    (not in stage list — skip)

**Commit message**:
---
<type>: <description>

<body if present>
---

操作: yes / edit message / edit files / cancel
<若 BLOCK>:  ⚠ 存在 CRITICAL/HIGH finding，默认建议 cancel，必须显式输入 "yes anyway" 才继续
<若 SPLIT>:  ⚠ 建议拆分，参考上面的拆分建议，用 edit files 选择子集
```

**默认答案根据 Recommendation 调整**：

- `PROCEED` → 默认 `yes`
- `PROCEED_WITH_CAUTION` → 默认 `yes`，显式提醒 MEDIUM findings
- `BLOCK` → 默认 `cancel`，需要 `yes anyway` 覆盖
- `SPLIT` → 默认 `edit files`

**操作语义**:

- `yes` / `yes anyway` → step 6
- `edit message` → 让用户直接编辑消息，再次展示
- `edit files` → 让用户选择性从 stage 列表里加 / 减文件；然后**重新跑 step 4.5 的审查**（因为范围变了），再展示
- `cancel` → 停止，不做任何变更

### 6. Stage 文件

**禁止** `git add -A` / `git add .` / `git add -u`。只用显式列出的路径：

```bash
git add <path1> <path2> <path3> ...
```

一个 `git add` 命令列出所有路径，或者多个 `git add` 分别处理子集（例如用户想多次提交），**都必须是绝对路径或仓库相对路径，禁止 glob**。

### 7. 创建 commit

用 heredoc 传递消息以保留多行格式（参照项目 git-workflow 惯例）：

```bash
git commit -m "$(cat <<'EOF'
<type>: <description>

<body>
EOF
)"
```

严禁：

- `--amend`（会改历史；**不管 pre-commit hook 失败还是其他情况，永远创建 NEW commit**）
- `--no-verify`（跳过 hook 就是隐藏问题）
- `--no-gpg-sign`（项目可能依赖签名）
- `-c commit.gpgsign=false`

### 8. 验证并报告

```bash
git status
git log -n 1 --format='%h %s'
```

给用户：

```
## 提交完成

✓ abc1234 <type>: <description>

工作区: clean / 还有 N 个未 staged 文件
远端状态: <git status 的 ahead/behind 信息>

下一步可选:
- git push  — 推送到远端（本命令不自动 push）
- /commit-changes  — 继续提交剩余变更
```

---

## Pre-commit hook 失败处理

若 `git commit` 因为 pre-commit hook 失败返回非 0:

1. **不要** 用 `--amend` 或 `--no-verify`
2. 读取 hook 的输出，定位失败原因（formatter / linter / typechecker / test）
3. 修复**根因** — 通常是：
   - 格式问题 → 运行 formatter，然后重新 `git add` 被 formatter 修改过的文件
   - Lint 警告 → 修复代码
   - 测试失败 → 修代码，不是禁用测试
4. **重新 stage** 被 hook 自动修改过的文件
5. **创建 NEW commit**（用 step 7 的 heredoc 模板重跑一次）
6. 循环直到 hook 通过

**关键理解**：commit 失败意味着**之前的 commit 从未产生**。这时 `--amend` 会改到**上一个已存在的** commit，完全是错的 — 用户会丢失历史。

---

## Guardrails

- **永远确认一次**: step 5 的 yes/edit/cancel 不能跳过
- **只 stage 显式列出的文件**: 禁止 `-A` / `.` / `-u`
- **敏感文件拦截**: step 2 扫描永远不能关闭
- **消息风格对齐**: type 必须在 `feat/fix/refactor/docs/test/chore/perf/ci` 里（与 step 4 推断表 + `GUIDES.md` 快速参考表一致）
- **不自动 push**: 本命令的最后一步是 `git log`，不是 `git push`
- **禁止 amend**: 永远创建新 commit
- **禁止跳过 hooks**: 尊重 pre-commit / commit-msg / pre-push
- **默认跑 pre-commit-reviewer**: 除非显式 `--skip-review`，永远在 step 5 之前调用子代理
- **BLOCK 时拒绝 yes**: 子代理返回 BLOCK 时默认答案是 cancel，必须用 `yes anyway` 才能覆盖
- **单主题 commit**: 发现跨 3+ 分组时必须提示用户拆分
- **不污染历史**: 消息必须有意义，拒绝 "update" / "wip" / "misc fixes"
- **尊重 .gitignore**: 被 ignore 的文件自动跳过，禁止 `git add -f`
- **不操作分支状态**: 本命令只做 stage + commit，不切分支、不 rebase、不 merge、不 reset
- **不改 git config**: 绝对禁止 `git config` 任何写操作
- **中途出错停下**: 任何 git 命令返回非 0 时停下报告给用户，禁止自作主张重试

## 典型场景

- 写完代码想立即提交 → `/commit-changes`
- 已经手动 stage 了一些文件，想直接用它们生成消息提交 → `/commit-changes`（它会识别已 staged 的子集）
- 想用自己的消息覆盖起草结果 → `/commit-changes "feat: explicit custom message"`
- 一次 session 改动跨多个主题 → 反复调用 `/commit-changes`，每次用 `edit files` 只 stage 一个主题的文件子集

## 与其他命令的协作

- `/commit-changes` 是**所有其他命令之后**的常规收尾
- `/draft-req` / `/opsx:archive` / `/finish-req` / `/update-notes` 都只写文件，不自己 commit — 走完它们的流程后用本命令统一提交
- 一次 session 生产多个独立主题时，用 `edit files` 模式分多次 commit，保持历史清爽
