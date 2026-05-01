---
name: "Init Notes"
description: Scan the project and generate the initial notes/README.md, get-started.md, and subsystems/*.md by reading code + build config.
category: Notes
tags: [notes, bootstrap, scan]
---

**扫描当前项目**，产出第一版"速览 / GetStarted / 子系统设计"文档。适用于刚装完 notes-scaffold、还只有占位 README 的项目。

**Input**: 无参数
- `/init-notes` — 完整扫描 + 起草

**IMPORTANT**: 这个命令**只写文档**，不改代码、不改 `notes/requirements/`、不动 `openspec/`。只落在 `notes/README.md`、`notes/get-started.md`、`notes/subsystems/*.md`、`notes/subsystems/index.md`、`notes/nav.yml`。

详细流程见 `.codex/skills/init-notes/SKILL.md`。
