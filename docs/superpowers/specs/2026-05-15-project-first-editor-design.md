# Project-First Editor Design

## Context

`lxe_editor` currently presents editable scene documents as two source kinds:

- `asset`: built-in scene files under `assets/scenes/`, treated as protected examples.
- `local`: user scene files under `data/scenes/`, treated as editable documents.

That model is becoming too small for the editor. A real editing workspace is not just one `.scene.yaml`; it can contain multiple scenes, private assets, editor state, and project metadata. Built-in starter content is also better described as a read-only template, not as an asset scene that is later redirected to local storage.

This design replaces the scene-source model with a project-first model:

```text
project_template  -> read-only starter package
project           -> editable user workspace
scene             -> document inside a project
```

There is no compatibility layer for the old `asset/local` model. Removing it early is intentional: the older model leaks into command behavior, API state, documentation, and save rules, and keeping it would preserve bugs around source kind redirection.

## Goals

1. Make `project` the editor's top-level working unit.
2. Make `project_template` the only read-only starter concept.
3. Allow one project to contain multiple scenes.
4. Keep scene commands, but change their meaning to operate inside the current project.
5. Remove `asset/local` source kind semantics from code, API, docs, and tests.
6. Avoid compatibility shims for old `assets/scenes` and `data/scenes` flows.

## Non-Goals

- No migration command for old `data/scenes/*.scene.yaml`.
- No legacy `scene load <arbitrary old scene path>` support.
- No editor UI file browser requirement in the first implementation.
- No general asset import pipeline; project-private assets can exist as files, but advanced import remains future work.

## New Vocabulary

| Term | Meaning |
|---|---|
| `project_template` | Read-only starter package under `assets/project_templates/<type>/` |
| `project` | Editable workspace under `data/projects/<project-id>/` |
| `project.yaml` | Project metadata file |
| `project_template.yaml` | Template metadata file |
| `scene` | A `.scene.yaml` document inside a project, usually under `scenes/` |
| active scene | The scene currently loaded into `SceneRuntime` |

## Directory Shape

Template packages:

```text
assets/project_templates/empty/
  project_template.yaml
  scenes/main.scene.yaml
  assets/

assets/project_templates/basic-3d/
  project_template.yaml
  scenes/main.scene.yaml
  assets/materials/
  assets/models/
```

Editable projects:

```text
data/projects/my_project/
  project.yaml
  scenes/main.scene.yaml
  scenes/lighting_test.scene.yaml
  assets/
  editor_state.yaml
```

System-wide built-in resources can still be referenced by stable built-in URIs or shared `assets/...` paths. Template-private resources are copied into the new project folder during `project init`.

## Project Documents

`project_template.yaml` should be small and explicit:

```yaml
schema: lxe.project_template.v1
id: basic-3d
displayName: Basic 3D
defaultScene: scenes/main.scene.yaml
copy:
  - scenes/
  - assets/
```

`project.yaml` records editable project metadata:

```yaml
schema: lxe.project.v1
id: my_project
displayName: My Project
activeScene: scenes/main.scene.yaml
scenes:
  - id: main
    path: scenes/main.scene.yaml
  - id: lighting_test
    path: scenes/lighting_test.scene.yaml
assetRoots:
  - assets/
createdFromTemplate: basic-3d
```

The implementation should not infer project identity from a lone scene file. A project is a folder with `project.yaml`.

## Startup Behavior

On editor startup:

```text
read data/lxe_editor/editor_data.yaml
  -> if lastProject exists and project.yaml is valid:
       open that project and load its activeScene
  -> otherwise:
       create an empty transient runtime scene
```

The empty transient scene is not a saved project. `project save` should fail with a clear message until a project is initialized or opened.

No template is auto-instantiated at startup. Creating a project is an explicit command.

## Command Surface

Project commands become the primary user-facing workflow:

```text
project templates
project list
project init <template-type> [project-name]
project open <project-id-or-path>
project save
project save-as <project-name-or-path>
project status
project close
```

Scene commands remain, but their semantics change. This is not a compatibility promise; the command names stay because users still need to operate on scenes, while the meaning moves from "load/save a standalone scene source" to "operate on scenes owned by the current project":

```text
scene list
scene open <scene-id-or-path>
scene save [scene-id-or-path]
scene new <scene-id>
scene duplicate <source-scene-id> <new-scene-id>
scene remove <scene-id>
scene status
```

Scene commands are scoped to the current project:

- `scene list` lists scenes from `project.yaml`.
- `scene open` loads another scene inside the current project.
- `scene save` writes the active scene inside the current project.
- `scene new` creates a scene document inside `data/projects/<project>/scenes/`.
- Without an open project, scene-mutating commands fail and suggest `project init`.

`scene load` should be removed or replaced by `scene open` in the same cleanup. Do not keep `scene load` as a compatibility alias, because the old name implies arbitrary-path loading outside project ownership.

## Save Semantics

There is no more “asset scene redirects to local scene” rule.

```text
project save
  -> save project.yaml
  -> save active scene document
  -> save project-scoped editor_state.yaml
  -> update lastProject
```

```text
scene save
  -> save active scene document inside current project
  -> mark project clean only if project metadata is also clean
```

Dirty state should be project-level, with enough internal tracking to know whether project metadata, active scene, or project editor state changed. The first implementation may expose a single project dirty flag if tests cover the visible behavior.

## Code Architecture

Replace the current scene-source objects:

```cpp
SceneSourceKind::Asset
SceneSourceKind::Local
SceneCatalog
SceneSession
```

with project-first objects:

| Object | Responsibility |
|---|---|
| `ProjectTemplateCatalog` | Enumerate read-only `assets/project_templates/` |
| `ProjectCatalog` | Enumerate `data/projects/` |
| `ProjectTemplateDocument` | Parse/write template metadata shape, read-only in editor flow |
| `ProjectDocument` | Parse/write `project.yaml` |
| `ProjectSession` | Current project path, active scene, dirty state, init/open/save/close decisions |
| `SceneRuntime` | Continue converting scene document to runtime scene and back |

`SceneRuntime` should not know whether a scene came from a template or a user project. It only receives a scene document path and resolves project-relative asset roots through explicit context.

`LxeEditorSession` remains the composition root, but its scene-session responsibilities move to `ProjectSession`.

## API And Events

Replace API state fields based on source kind:

```json
{
  "sourceKind": "asset|local",
  "currentDocumentPath": "..."
}
```

with project state:

```json
{
  "project": {
    "id": "my_project",
    "displayName": "My Project",
    "path": "data/projects/my_project",
    "dirty": true,
    "activeScene": "scenes/main.scene.yaml"
  }
}
```

Events should become project-aware:

| Event | Meaning |
|---|---|
| `ProjectInitialized` | `project init` created and opened a project |
| `ProjectOpened` | Current project changed |
| `ProjectSaved` | Project metadata and active scene were saved |
| `ProjectClosed` | Editor returned to transient empty scene |
| `ActiveSceneChanged` | Current project remains open, active scene changed |
| `SceneSaved` | Active scene document saved |

MCP and HTTP endpoints should reuse the same command surface. They must not keep a hidden asset/local pathway.

## Documentation Changes

Update current docs and tutorials to remove asset/local as the conceptual model:

- `notes/tutorial/start-project/03-load-and-save-scene.md`
- `notes/tutorial/start-project/index.md`
- `notes/tutorial/start-project/05-troubleshooting.md`
- `notes/design/editor-system/04-scene-runtime-and-persistence.md`
- `notes/design/editor-system/05-api-recording-and-observation.md`
- `notes/subsystems/scene.md`
- `src/demos/lxe_editor/README.md`

New teaching sequence:

```text
project templates
  -> project init empty my_project
  -> project save
  -> scene list
  -> scene new lighting_test
  -> scene open lighting_test
```

The docs should emphasize:

- Templates are read-only.
- Projects are the editable workspace.
- Scenes are documents inside projects.
- A project can contain more than one scene.

## Testing Strategy

Add or update tests around:

- Template catalog enumerates `assets/project_templates`.
- `project init <type> [name]` creates `data/projects/<id>/project.yaml`, copies template scene/assets, opens active scene, updates `lastProject`.
- Startup opens `lastProject` when valid.
- Startup creates transient empty scene when no project exists.
- `project save` fails without an open project.
- `scene list/open/save/new/duplicate/remove` are scoped to the current project.
- API state reports `project`, not `sourceKind`.
- No `asset` / `local` source kind remains in editor API snapshots, scene catalog, or user-facing docs.

## Implementation Defaults

1. Project IDs are filesystem-safe slugs derived from the requested project name, using lowercase letters, numbers, `_`, and `-`. Collisions under `data/projects/` receive numeric suffixes such as `my_project-2`.
2. Scene paths stored in `project.yaml` are project-relative. A scene command accepts either a scene id from `project.yaml` or a project-relative path under the project root.
3. Project-private asset paths resolve relative to the project root first. Shared built-in assets may still use explicit shared paths such as `assets/...`; template-private assets are copied into the project during `project init`.
4. Dirty tracking starts with a visible project-level dirty flag. Internally, `ProjectSession` should distinguish project metadata, active scene document, and editor state so later per-scene dirty status does not require another command/API redesign.
5. Old `asset/local` identifiers, enum values, API payloads, and documentation terms should be deleted during implementation instead of hidden behind aliases.
