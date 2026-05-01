---
name: update-roadmap
description: Discuss and directly update notes/roadmaps/*.md by cross-checking against notes/requirements/ (active + finished). Use when the user wants to add a new phase, reconcile roadmap with what's actually shipping, or prune stale items. Writes to roadmap files directly after user confirmation.
---

Update `notes/roadmaps/` by reconciling it with `notes/requirements/`. Discuss with the user, then edit the roadmap files directly.

## When To Use

Trigger this skill when the user:

- wants to add or split a roadmap phase
- asks to align roadmap items with current `notes/requirements/`
- wants to mark done items as completed (or prune items that were abandoned)
- says something like "check roadmap drift" / "update roadmap"

Do not use this skill to generate subsystem design docs (that's `/init-notes` or manual authoring) or to draft requirements (that's `/draft-req`).

## Scope

Files this skill is allowed to edit:

- `notes/roadmaps/*.md`
- `notes/roadmaps/README.md` (only to refresh the phase index, if present)

Never edit `notes/requirements/`, `openspec/`, source code, or `notes/subsystems/`.

## Required Workflow

### 1. Load the roadmap corpus

- `Glob notes/roadmaps/*.md` — all phase/theme files
- `Read` each — extract: phase name, stated goals, listed items, status markers

If the directory only has `README.md` (empty roadmap), ask the user what the first phase should be about before proceeding.

### 2. Load the requirements corpus

- `Glob notes/requirements/*.md` — active requirements
- `Glob notes/requirements/finished/*.md` — archived ones

For each file read the header + "实施状态" (implementation status) block. Build an index:

```
REQ-NNN | title | status (未开始 / 进行中 / 已完成) | location (active or finished)
```

### 3. Cross-check for drift

Produce a diff table:

| Roadmap item | Matching REQ(s) | Status | Action |
|---|---|---|---|
| ... | REQ-005, REQ-007 | 已完成/已完成 | mark done |
| ... | REQ-012 | 进行中 | keep as in-progress |
| ... | (none) | — | either drop or ask the user |

Also flag:

- **Orphan REQs**: finished or in-progress requirements that no roadmap item points to. Ask the user whether they belong in an existing phase or a new one.
- **Stale goals**: roadmap items whose rationale is contradicted by a shipped/finished REQ.
- **Scope mismatch**: roadmap item too vague to be mapped to any REQ, or too narrow (really a single REQ masquerading as a phase).

### 4. Propose changes

Write a change plan before touching files:

```
## Roadmap change plan

notes/roadmaps/phase-01-foundation.md
  - mark item "初版资源加载" 完成 (REQ-003, REQ-005 已 finished)
  - 移除 item "XYZ" —— 上游方向已放弃
  - 新增 item "frame graph 扩展" —— 来自 REQ-011 (进行中)

notes/roadmaps/phase-02-materials.md (新建)
  - 目标: <1 段>
  - 涵盖 REQ-008, REQ-009, REQ-012
```

Ask the user via **AskUserQuestion**:

> 按这份 plan 改？
> - yes — 全部落地
> - edit — 修改某一条
> - abort

### 5. Discuss open questions

If step 3 surfaced ambiguity, **stop** and ask the user before guessing:

- "REQ-014 没映射到任何阶段，属于 phase-02 还是单开一篇？"
- "phase-01 里这条 'tech debt 清理' 还作数吗？找不到对应的 REQ。"
- "阶段顺序是否需要调整？"

Never silently invent new phases. Never silently drop items.

### 6. Apply edits

Use `Edit` for in-place modifications. Use `Write` only for brand new phase files.

File conventions:

- filename: `phase-NN-<kebab-case-theme>.md` (e.g., `phase-01-foundation.md`, `phase-02-materials.md`)
- top-level heading: `# Phase NN: <主题>`
- sections: 目标 / 范围 / 里程碑 / 关联需求 / 完成判据

When marking items done, prefix with `- [x]` and keep the REQ references so readers can trace back.

### 7. Keep nav.yml in sync

`notes/nav.yml` uses a `@roadmaps` placeholder that auto-expands `notes/roadmaps/*.md` (excluding `README.md`). **Do not edit nav.yml** for roadmap changes — the generator picks up new files automatically.

### 8. Report

After writing, summarise:

```
## update-roadmap 完成

改动:
- notes/roadmaps/phase-01-foundation.md — <一句话>
- notes/roadmaps/phase-02-materials.md — 新建

未决项:
- REQ-014 仍未映射（用户决定先搁置）
```

## Guardrails

- **直接改文件**，但**改之前必须先出 plan 让用户点头**（step 4）
- **不改需求文档**：发现 `notes/requirements/*.md` 里的事实和 roadmap 冲突，报告给用户，不要自作主张改 REQ
- **不改代码**
- **引用带编号**：roadmap 里提到某件事来自哪里时，写 `REQ-NNN`，不要写"上次那个改动"
- **不要保留 tombstone**：被取消的 item 直接删掉（历史留给 git log），不要写"~~已取消~~"
- **找不到匹配时停下问**：别假设没匹配到的 item 可以默默删掉，也别假设 orphan REQ 属于某个阶段
- **中文为主**：和 `notes/roadmaps/README.md`、`notes/requirements/` 保持一致

## Output Style

- Always surface the drift table before the change plan — show the user what's out of sync, then what you'd do about it.
- When an action depends on user judgment, list the options explicitly.
- Do not describe the change plan as "a full roadmap sync" when only one phase file changed.
