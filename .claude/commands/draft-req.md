---
name: "Draft Requirement"
description: Interactive discussion to form a new requirement doc and save it to docs/requirements/
category: Requirements
tags: [requirements, planning, discuss]
---

和用户进行**交互式讨论**，把一个改动想法打磨成正式的需求文档，最终写入 `docs/requirements/`。命名、编号、段落结构都对齐已有的 `finished/*.md`。

**Input**: 可选一句话 brief
- `/draft-req` — 完全交互，从零开始问
- `/draft-req "支持 instanced rendering"` — 有初始种子，直接进入 discovery

**IMPORTANT**: 这个命令**只产出文档**。不生成代码、不触发 `/opsx:propose`、不改动任何已有的需求文件。完成后由用户决定下一步（`/finish-req` 或 `/opsx:propose`）。

---

## Steps

### 1. 扫描现有需求库

先建立上下文：

- `Glob docs/requirements/*.md` — 活动中的需求
- `Glob docs/requirements/finished/*.md` — 已归档的需求

对每个文件，读头部 + "实施状态" 段，提取：
- 编号（`NNN` 或 `NNNa/b/c`）
- 标题
- 状态（未开始 / 进行中 / 已完成）

这份清单用于：
- 计算下一个可用编号（step 9）
- Phase 5 讨论依赖 / 下游时提示给用户
- Phase 6 冲突扫描

### 2. 没有 brief 时先问主题

若 `$ARGUMENTS` 为空，用 **AskUserQuestion** 提一个开放问题：

> 这次的需求大致是关于什么？用一两句话描述你想要建或改的东西。

把用户回复作为 seed brief。

### 3. Phase 1 — 动机

用 **AskUserQuestion** 依次问（可合并成一条多填问卷）：

1. **当前痛点**: 现在哪里不对？不做这件事接下来的工作会在哪里卡住？
2. **触发时机**: 为什么现在做？是否有刚落地的上游 REQ 让这件事变可行？
3. **失败模式**: 不做的后果具体是什么？（bug / 维护负担 / 无法扩展某特性）

把答复结构化记在心里，后面喂给"背景"段落。

### 4. Phase 2 — 用代码验证现状

基于用户描述的痛点，主动 **Grep / Read** 定位 `src/` 里相关的类、文件、行号。目的是：

- 确认痛点描述准确（代码里确实是这样）
- 找出受影响文件的完整清单（后面的 R 要列出来）
- 发现描述和代码脱节时**停下反问**，不要在错误前提下写需求

报告给用户：

```
## 现状定位

你描述的问题对应到代码里：
- src/core/asset/foo.hpp:42 — `class Foo` 的 `bar()` 方法
- src/infra/loaders/foo_loader.cpp:15 — 唯一调用方
- src/test/integration/test_foo.cpp — 相关测试

这是你想改的全部范围吗？还有别的地方吗？
```

用户补充的文件也过一遍 Grep，保持一致。

### 5. Phase 3 — 目标状态

用 **AskUserQuestion**：

1. **成功标准**: 落地后"好"的状态是什么？给出 2-4 条**可验证**的语句（"某类存在" / "某函数被删" / "某测试通过"）
2. **不变的东西**: 哪些现有行为必须保持不变？（兼容性边界）
3. **API 变化**: 调用方需要改代码吗？范围多大？

### 6. Phase 4 — 分解为 R1..Rn

基于前三个 phase，**你**起草第一版 R 分解，给用户看：

```
## 初步分解

R1: <具体交付项>
R2: <具体交付项>
R3: <具体交付项>
...
```

和用户讨论直到认可。每个 R 必须：

- 有明确的验收标准，能被 `/finish-req` 的 verification 流程独立检查
- 颗粒度适中（太粗 → 不好验证；太细 → R 数量爆炸）
- 代码引用带行号（`src/core/foo.hpp:42`），行号从 Grep 结果拿，不要编造
- 如果涉及接口签名，给出 C++ 代码片段示例（遵循 finished/004-007 的风格）

### 7. Phase 5 — 边界、依赖、下游

用 **AskUserQuestion**：

1. **明确不做**: 哪些相关但不在本次范围的事情要留给以后？
2. **上游依赖**: 需要哪些已完成 / 进行中的 REQ 先落地？（用 step 1 的清单提示用户）
3. **下游消费者**: 本需求落地后解锁哪些未启动的 REQ？

### 8. Phase 6 — 冲突扫描

写文件前主动扫描：

- `docs/requirements/finished/*.md` — 是否有归档项重叠（风险: 重复实现或重复废弃）
- 活动中的 `docs/requirements/*.md` — 是否有当前进行中的需求与本需求冲突
- `openspec/changes/**`（含 archive）— 是否有变更已经覆盖了本需求的一部分范围

报告发现，示例：

```
## 冲突扫描

⚠ REQ-003a（finished）R2 声明了 `X` 的废弃 — 本需求 R2 也涉及 X。
  → 建议本需求 R2 改为"在 X 已废弃的基础上扩展 Y"，避免重复。

⚠ openspec/changes/archive/2026-04-13-unify-material-system 涉及 MaterialInstance
  → 本需求对 MaterialInstance 的改动需要在此之上。
```

冲突严重时**停下**让用户决定是否调整范围或放弃本次需求。

### 9. 计算编号 + 文件名

规则：

- 从 step 1 的扫描结果提取形如 `NNN[a-z]?-*.md` 的前缀
- 下一个编号 = `max(numeric prefix) + 1`
- 例外：若用户明确说"这是 REQ-008 的前置拆分"或"这是 REQ-008 的后续补丁"，允许 `008a` / `008b` 变体
- 文件名: `<NNN>-<kebab-case-title>.md`（标题小写 + 连字符，对齐已有文件命名）
- 文档标题: `# REQ-<NNN>: <中英文标题>`

用 **AskUserQuestion** 让用户确认编号 + 文件名 + 标题。

### 10. 按模板起草文件

严格遵循现有 `docs/requirements/finished/` 的结构：

```markdown
# REQ-<NNN>: <Title>

## 背景

<3-8 句，来自 Phase 1 的痛点 + 触发时机。带上 src/... 的具体引用。>

## 目标

1. <来自 Phase 3 的成功标准 1>
2. <成功标准 2>
...

## 需求

### R1: <简短名称>

<详细描述 + 可验证的交付项 + C++ 代码片段示例（如涉及接口签名）>

### R2: <简短名称>

...

### Rn: ...

## 测试

- <Phase 3 验收标准对应的测试入口，指向具体 `src/test/integration/test_*.cpp`>
- <若需要新建测试文件，说明期望覆盖的场景>

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/...` | <简短描述> |
| `src/infra/...` | ... |
| `src/test/integration/test_*.cpp` | ... |

## 边界与约束

- <Phase 5 的"明确不做"逐条列出>
- <其他技术约束（性能 / 兼容性 / 层级依赖）>

## 依赖

- **REQ-NNN**（已完成 / 进行中）— 为什么硬依赖
- 外部条件（如 shader 工具链版本）

## 下游

- <本需求解锁的后续工作，点名具体的未启动 REQ 或概念>

## 实施状态

未开始。
```

每段都从前几轮讨论的具体答复中提炼，不要写占位符。

### 11. 预览 + 迭代

把完整草稿展示给用户：

```
## 草稿就绪

文件: docs/requirements/<NNN>-<title>.md

[完整内容]

操作: yes / edit <section> / redo
```

- `yes` — 调用 Write 落地
- `edit <section>` — 只重写指定段（背景 / 目标 / R1 / 依赖 / ...），然后再次预览
- `redo` — 回到 step 6 重新分解 R

### 12. 写入 + 总结

`Write docs/requirements/<NNN>-<title>.md`

报告：

```
## draft-req 完成

已创建: docs/requirements/<NNN>-<title>.md
- 编号: REQ-<NNN>
- 标题: <title>
- 需求项: R1..R<N>
- 上游依赖: <REQ-XXX, REQ-YYY>
- 下游解锁: <REQ-ZZZ, ...>

下一步可选:
- /opsx:propose — 把需求转成 openspec change 开始实施
- /finish-req <NNN> — 代码落地后用它做 verification + 归档
```

---

## Guardrails

- **讨论价值在前半段**: Phase 1-5 是这个命令的核心，不要为了快把它压缩成"你要什么我就写什么"。用户如果想直接跳到生成，让他用 `/opsx:propose`。
- **代码引用必须真实**: 每次写 `src/... :LINE` 之前必须用 Grep 确认过行号，禁止编造。
- **中文为主**: 匹配现有 `docs/requirements/` 风格。代码符号 / 类名 / 文件名保留英文原形。
- **只落在顶层**: 新需求永远写到 `docs/requirements/`，不写 `finished/`。只有 `/finish-req` 有权移入 `finished/`。
- **不改已有需求**: 发现冲突时报告 + 建议，但**禁止修改**现存的 `*.md` 文件 — 让用户决定是否人工介入。
- **一次只写一份**: 发现讨论范围过大覆盖了两个独立话题，停下建议拆分成两次 `/draft-req`。
- **不代用户做权衡**: Phase 5 的"明确不做"必须由用户点头，不要自己默默砍范围。
- **不触发代码修改**: 这个命令的输出是唯一一个新 `.md` 文件，仅此而已。
- **若用户中途问别的问题**: 答完后明确回到"我们刚才在 Phase <N>，要继续吗？"
- **尊重已归档的历史**: 发现本次讨论涉及的模块被某个 finished REQ 明确规定过，优先引用而非改写历史。

## 编号冲突的处理

Step 9 计算的下一个编号可能和用户心里想的不一致（用户可能想叫 `009a` 表示它是 `009` 的变体）。**永远问一次**，不要默默用 `max+1`。

极少数情况下用户会要求插入一个"历史补丁"（例如 REQ-004a 补充 REQ-004 的遗漏项），这时允许用字母后缀，但必须：

- 先确认 REQ-004 存在且状态合适
- 在新文档的"依赖"段明确写"作为 REQ-004 的补充"
- 文件名用 `004a-<title>.md`

## 使用场景

- **新功能想法成熟到可以写需求了** → `/draft-req "支持 instanced rendering"`
- **重构想法还在雏形** → `/draft-req` 从零开始
- **发现某个大 REQ 应该拆成子项** → 先在原 REQ 讨论，然后用 `/draft-req` 形成拆出来的子需求
- **代码实施过程中发现漏了一环** → `/draft-req "REQ-XXX 补充: 处理边界条件"`
