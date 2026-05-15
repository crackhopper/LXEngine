# Tutorial Rewrite and Authoring Skill Design

## Goal

Replace the current single-line PBR tutorial with a teacher-style tutorial system that helps a newcomer learn LXEngine through the current `lxe_editor` workflow, then progressively extend materials, lights, editor commands, and scene node kinds.

The rewrite must also create a reusable Codex skill, `tutorial-authoring`, so future tutorial edits follow the same tone, structure, and current-code grounding.

## Scope

### In scope

- Rewrite `notes/get-started.md` as the tutorial entry page.
- Replace the current `notes/tutorial/00-06` PBR series with five tutorial series.
- Update `notes/nav.yml` so the new series replaces the old flat tutorial list.
- Add active requirements under `notes/requirements/` for future-facing tutorial capabilities that do not exist yet.
- Add `.codex/skills/tutorial-authoring/SKILL.md`.
- Validate the notes site build.

### Out of scope

- Implementing engine features described by future-facing tutorials.
- Changing source code, assets, shaders, or editor behavior.
- Preserving the old PBR tutorial as a separate active tutorial path.
- Reworking roadmap phase numbering in the same change.

## Tutorial Structure

### Entry page

`notes/get-started.md` becomes the front door:

- What LXEngine is today.
- The fastest path to build and open `lxe_editor`.
- A map of the five tutorial series.
- A clear distinction between current capabilities and future tutorial workflows.
- Links to release notes, concepts, subsystem docs, and requirements.

### Series 1: Start Project

Directory: `notes/tutorial/start-project/`

Purpose: teach the minimum operational path.

Planned pages:

- `index.md`: series map and learning goal.
- `01-environment-and-build.md`: Linux build prerequisites, CMake/Ninja, shader compile path.
- `02-start-editor.md`: build and run `lxe_editor`.
- `03-load-and-save-scene.md`: load built-in scene, save local scene, reopen.
- `04-basic-authoring.md`: create primitive/camera/light, select, move, duplicate, preview.
- `05-troubleshooting.md`: common install, Vulkan, shader, asset-root, and window issues.

### Series 2: Custom Material

Directory: `notes/tutorial/custom-material/`

Purpose: teach material authoring through the current material asset path.

Planned pages:

- `index.md`: material as a recipe analogy.
- `01-material-building-blocks.md`: shader, `.material`, template, instance, parameters.
- `02-material-yaml-and-shader-contract.md`: YAML-to-code and shader reflection contract.
- `03-start-from-rtr-template.md`: use `rtr_experiment_template.material`.
- `04-write-gooch-shader.md`: implement a simple Gooch-style shader.
- `05-verify-in-editor.md`: assign material, tune node-level parameters, save/reload.
- `06-debug-material-problems.md`: compile, reflection, binding, parameter, and visual failures.

This series should use only currently existing capabilities where possible.

### Series 3: Custom Light

Directory: `notes/tutorial/custom-light/`

Purpose: teach current light data and a future-friendly extension path.

Planned pages:

- `index.md`: light as a lamp analogy.
- `01-current-light-model.md`: `DirectionalLight`, `PointLight`, `SpotLight`, `SceneLightsUBO`, scene document `light.kind`.
- `02-define-lights-in-scene-yaml.md`: use `.scene.yaml` to define light nodes.
- `03-extend-light-in-cpp.md`: how a new light type would touch core light data, scene runtime, inspector, command bus, and shader contract.
- `04-future-light-assets.md`: future workflow for light presets / light assets.
- `05-debug-light-problems.md`: visibility, binding, runtime capture, editor helper, and shader mismatch checks.

This series must label pages 03-04 as partially future-facing where the smooth registry/preset workflow does not exist yet.

### Series 4: Extend Editor Toolbar / Command

Directory: `notes/tutorial/extend-editor/`

Purpose: teach command-first editor extension.

Planned pages:

- `index.md`: toolbar as a remote control, command bus as the wiring.
- `01-command-first-editor.md`: why UI actions dispatch commands.
- `02-add-a-command.md`: add and register a command handler.
- `03-add-a-toolbar-button.md`: add a toolbar button that dispatches a command.
- `04-connect-undo-api-and-mcp.md`: command history, undo/redo, API, and MCP reuse.
- `05-debug-editor-extension.md`: command parsing, completion, UI state, and API trace.

This series can describe an ideal extension registry as future-facing and link to a requirement.

### Series 5: Extend Scene Node

Directory: `notes/tutorial/extend-scene-node/`

Purpose: teach how a new node kind should fit the existing scene/editor workflow.

Planned pages:

- `index.md`: scene node as an actor on stage.
- `01-scene-node-contract.md`: `SceneNode`, components, transform, path, document, runtime.
- `02-design-a-new-node-kind.md`: choose data, component/runtime representation, save format, editor surface.
- `03-save-load-and-command-support.md`: scene document, scene runtime, command bus, inspector.
- `04-debug-draw-and-picking.md`: debug helper, bounds, picking, selection visuals.
- `05-compatible-editor-operations.md`: rename, duplicate, copy/paste, undo/redo, API state.

This series should make future registry points explicit, because current support is hand-wired rather than plugin-like.

## Requirement Documents

The new tutorials may teach future workflows only when an active requirement exists and is linked from the relevant page.

Create a local suffix family after the finished `041-*` scene-authoring work:

- `notes/requirements/042-a-tutorial-light-asset-and-custom-light-registry.md`
- `notes/requirements/042-b-tutorial-editor-extension-registry.md`
- `notes/requirements/042-c-tutorial-custom-scene-node-registry.md`

These requirements are documentation-support requirements, not immediate code implementation promises. Each should include:

- background: tutorial wants to teach a smooth workflow, but current code is hand-wired
- goals
- `R1..Rn`
- tests
- modification scope
- boundaries
- dependencies
- implementation status: `未开始`

They should use `REQ-042-a`, `REQ-042-b`, and `REQ-042-c` references in tutorial pages.

## Writing Style

All tutorial pages should follow these rules:

- Chinese-first prose.
- Teacher voice: patient, incremental, explanatory.
- First-person plural `我们`, not second-person `你`.
- Start from a concrete analogy before code.
- Each new concept gets three steps: why it exists, where it lives in this repo, how we touch it.
- Use compact tables for parallel objects and API/field lists.
- Use annotated YAML when the concept has a YAML surface.
- Avoid presenting future behavior as current reality.
- Every future-facing section must have a visible note and a requirement link.
- Prefer `lxe_editor` workflows over old standalone render examples.
- End each page with “我们已经学会了什么” and “下一步”.

## `tutorial-authoring` Skill

Create `.codex/skills/tutorial-authoring/SKILL.md`.

Trigger when:

- writing or editing `notes/tutorial/**`
- rewriting `notes/get-started.md`
- adding tutorial-support requirements
- user asks for teacher-style, newcomer-friendly LXEngine tutorial content

The skill should instruct Codex to:

- read `AGENTS.md`, `openspec/specs/notes-writing-style/spec.md`, current target tutorial pages, and directly relevant code/docs
- validate current capability from code before making claims
- keep current and future workflows clearly separated
- add/link requirements for future capabilities
- update `notes/nav.yml` when tutorial structure changes
- run `scripts/notes/serve_site.sh --build` after edits

Keep the skill concise; do not add extra README files.

## Navigation

Replace the current flat Tutorial nav with grouped series:

```yaml
- Tutorial:
    - 总览: tutorial/index.md
    - 启动项目:
        - tutorial/start-project/index.md
        - ...
    - 自定义材质:
        - tutorial/custom-material/index.md
        - ...
    - 自定义灯光:
        - tutorial/custom-light/index.md
        - ...
    - 扩展编辑器:
        - tutorial/extend-editor/index.md
        - ...
    - 扩展场景节点:
        - tutorial/extend-scene-node/index.md
        - ...
```

`notes/tutorial/index.md` should become the tutorial hub and explain the recommended reading order.

## Validation

After implementation:

- Run `scripts/notes/serve_site.sh --build`.
- Check generated navigation includes all tutorial series and the new requirements.
- Check all new requirement files appear under the generated requirements index.
- Search for stale old tutorial filenames in `notes/nav.yml` and `notes/tutorial/`.
- Spot-check pages for:
  - no unmarked future claims
  - requirement links for future features
  - teacher voice and `我们` narration
  - no links to active `041-d` to `041-h` paths, because those are finished

## Open Questions Resolved

- Old tutorial handling: replace the old flat PBR tutorial; reuse useful concepts inside the new custom-material series.
- Future capabilities in tutorials: allowed for series 3-5 only when clearly marked and backed by active requirements.
- Skill location: project-local `.codex/skills/tutorial-authoring/`.

