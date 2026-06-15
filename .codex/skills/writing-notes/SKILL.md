---
name: writing-notes
description: Use when writing or revising LXEngine notes under notes/, especially explanatory docs, design docs, tutorials, current-code walkthroughs, future requirement links, or multi-page learning paths.
---

Use this skill when writing or editing LXEngine notes: concepts, design docs, tutorials, subsystem explanations, get-started material, or requirements that support future-facing documentation.

## Inputs To Read

Always read:

- `AGENTS.md`
- the target note, index page, or navigation file being changed
- directly relevant current docs under `notes/concepts/`, `notes/subsystems/`, and `docs/superpowers/specs/`
- directly relevant code/assets before making current-behavior claims

## Core Rules

- Chinese-first prose.
- Use teacher voice for tutorials and explanatory design notes: patient, concrete, incremental.
- Use first-person plural `我们`; avoid second-person narration.
- Start new concepts with a concrete analogy, then map the analogy to real code/assets.
- Explain each new concept in the order that best matches how a newcomer would understand it: why it exists, what problem it solves, how this repo organizes it, then how we operate or change it.
- Choose section headings for the specific topic and causal flow. Do not mechanically reuse headings like `我们先建立一个心智模型` / `当前仓库里它在哪里` / `一步一步操作` on every page.
- Every page should read like a lesson with cause and effect: concept -> project organization -> concrete command/file/action -> verification -> next concept.
- For build and tooling tutorials, introduce the general tool first, then show how LXEngine uses it, then explain each command and option.
- Use tables for parallel concepts, fields, files, APIs, and current-vs-future comparisons.
- Use annotated YAML for `.material` and `.scene.yaml` surfaces.
- Prefer `lxe_editor` workflows over standalone render test examples.
- Describe only current code as current reality.
- Mark future-facing tutorial paths explicitly and link an active `REQ-NNN` / `REQ-NNN-a` requirement.
- Do not link active paths for finished requirements; use `notes/requirements/finished/` only when historical context is truly needed.
- End tutorial pages with `我们已经学会了什么` and `下一步`; design docs may end with `继续阅读`.

## Organizing Notes

- A single page should follow cause and effect: problem -> design pressure -> current structure -> data/control flow -> boundaries -> where to read code.
- A multi-page folder should follow total-to-detail order: index gives the map, early pages explain the main loop and ownership, later pages zoom into UI, commands, persistence, automation, and extension points.
- Use dependency order when teaching code: the page a reader needs first should appear first in nav.
- Keep file lists close to the concept that needs them. Do not dump a directory table before explaining why those files matter.
- For editor/system design docs, separate:
  - ownership/lifetime: who creates and owns objects
  - control flow: which event calls which command or service
  - data flow: which document/state/runtime object carries the data
  - observation flow: how tests/API/MCP see the result
- Index pages should say what the folder helps us understand, provide a reading order, and explain why that order is chosen.

## Workflow

1. Identify the note type: concept, design, tutorial, subsystem, requirement, or index.
2. Read current code/docs for all factual claims.
3. Separate current workflow from future workflow.
4. If a future workflow lacks a requirement, draft or request a requirement before teaching it as a path.
5. Write the page with the topic's natural causal structure, not a fixed template.
6. Update `notes/nav.yml` when adding/removing pages that should appear in the site navigation.
7. Run `scripts/notes/serve_site.sh --build`.
8. Report new pages, requirement links, and any existing site warnings that remain unrelated.

## Lesson Structure

Pick headings that fit the lesson. A good tutorial page usually contains these roles, but the headings should be topic-specific:

| Role | Better heading examples |
|---|---|
| Concept | `CMake 负责生成工程图` / `材质模板先决定结构` |
| Repo mapping | `LXEngine 的 CMake 入口如何分工` / `这些字段最终流向哪些 C++ 对象` |
| Guided work | `配置 build 目录` / `复制模板并改名` |
| Verification | `确认 shader 编译链路` / `保存后检查 scene YAML` |
| Troubleshooting | `CMake 配置失败时先看这些点` |

End pages with `我们已经学会了什么` and `下一步`, unless an index page is clearly better served by `继续阅读`.

Keep pages focused. Prefer multiple short tutorial pages over one long page that mixes concepts.
