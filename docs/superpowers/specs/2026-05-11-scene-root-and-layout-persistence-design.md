Date: 2026-05-11

## Context

`lxe_editor` currently exposes two editor ergonomics gaps:

1. The Scene Tree renders top-level scene nodes as a flat list. The codebase
   still relies on a synthetic path root for `findByPath("/")`, but that root
   is not a real scene node and is not shown in the UI.
2. The editor does not restore local UI state across launches. ImGui layout
   persistence is currently disabled, and the native window abstraction does
   not yet provide a restore path for saved window geometry.

The desired behavior is:

- scenes use a real explicit root node in both runtime data and serialized scene files
- the Scene Tree displays that root node directly
- `lxe_editor` restores local editor layout state from user data on the next launch
- layout persistence stays local to the machine and does not travel with scene assets

## Goals

- Replace the synthetic scene root model with a real explicit root node in `Scene`.
- Serialize and deserialize the root node as part of the scene document.
- Display the explicit root node in the Scene Tree UI.
- Persist `lxe_editor` layout state under `data/` and restore it on startup.
- Restore main window position, size, and maximized state in addition to ImGui panel layout.

## Non-Goals

- No persistence of current scene path, selected node, preview mode, or other editor session state.
- No per-scene UI layout overrides.
- No version-controlled `imgui.ini` workflow.
- No hidden UI-only fake root that differs from the runtime scene model.

## Explicit Scene Root

### Current Problem

Today the scene system mixes two root concepts:

- a synthetic internal root used only for path resolution
- a flat list of top-level runtime nodes exposed by `Scene::getRootNodes()`

That split leaks into the editor:

- `/` resolves conceptually, but it is not a real scene node
- the Scene Tree has no visible root row
- scene hierarchy semantics differ between path handling, UI, and serialized scene structure

### New Model

`Scene` must own one real explicit root node.

Properties of this root node:

- always exists for every scene
- is the sole parent of all scene top-level authored nodes
- participates in traversal, selection, and serialization
- is displayed in the Scene Tree
- is addressable as `/`

This root node is not a temporary helper. It is part of the formal scene model.

### Path Semantics

With the explicit root in place:

- `/` resolves to the root node
- direct children of the root keep paths of the form `/child_name`
- descendant paths remain `/parent/child`

The root node should not force a path prefix like `/root/world`. The user-facing
path system remains anchored at `/`.

### Scene Tree Behavior

The Scene Tree must render the root node as the top row.

Behavior:

- all authored scene nodes appear as children under that root
- expand/collapse operates on the real root node
- selection logic works on the root node the same way it works on other nodes,
  except for any existing command restrictions that intentionally block removal
  or destructive edits on the root

If destructive commands need restrictions, those restrictions should be explicit
and tied to the root node role, not to the absence of a parent.

## Scene Document Contract

The scene document must serialize the explicit root node.

This changes the full-scene asset schema introduced by the existing workspace design:

- the root becomes a first-class serialized node
- top-level authored nodes are stored as root children, not as an implicit file-level list

The schema should express one canonical hierarchy rooted at the explicit root
instead of keeping parallel concepts like `nodes:` plus a hidden runtime root.

The first-version serializer continues to store:

- node names
- parent/child hierarchy
- transforms
- supported scene components
- editor-only metadata

But now the hierarchy always starts from the explicit root node.

### Compatibility

Existing scene files without an explicit serialized root should be treated as a
legacy format.

Load behavior for legacy documents:

- create the explicit root node in memory
- attach the legacy top-level nodes as its children

Save behavior:

- always writes the new explicit-root format

This gives us one-way normalization without keeping two long-term save formats.

## Local Layout Persistence

### Scope

Layout persistence is local editor state only. It must live under `data/` and
must not be stored in scene files.

The restored state includes:

- main native window position
- main native window size
- main native window maximized state
- ImGui window positions
- ImGui window sizes
- ImGui collapsed state

The restored state does not include:

- current scene path
- selected nodes
- preview on/off
- command history
- other transient editing state

### Storage

`lxe_editor` should use a dedicated local layout file under `data/`.

Recommended shape:

- `data/lxe_editor/layout.ini` for ImGui layout state
- `data/lxe_editor/window_state.yaml` or equivalent small structured file for
  native window geometry and maximized state

The exact filenames may vary, but the design requires:

- they live under `data/`
- they are editor-local
- they are not committed to the repository

### ImGui Persistence

The current code disables ImGui ini persistence with `ImGui::GetIO().IniFilename = nullptr`.

That must change to a local user-data path or an equivalent explicit load/save flow.

Requirements:

- startup loads existing local ImGui layout if present
- shutdown saves current ImGui layout back to the same local file
- absence of the file is not an error; the editor falls back to default layout
- this change does not require true ImGui docking support; local persistence is
  defined in terms of ordinary ImGui window placement, size, and collapsed state

### Native Window Persistence

The window abstraction must grow enough API to restore:

- position
- size
- maximized state

At minimum, the SDL backend path used by current `lxe_editor` must implement
this behavior. If GLFW remains a supported backend in the repository, the same
interface contract should be implemented there too, so the abstraction stays honest.

Startup behavior:

- if local window state exists, restore it before or during first show
- if no local state exists, keep current default startup size/placement

Shutdown behavior:

- write the latest non-invalid window geometry
- record whether the window ended in maximized state

## Runtime Integration

The change should stay decomposed into three clear responsibilities:

### Scene model changes

- convert `Scene` to own a real root node
- update traversal and path helpers to use it directly
- update any logic that currently infers "top-level" from `parent == nullptr`

### Scene document changes

- update scene serialization to persist the explicit root
- load legacy flat-root documents by normalizing them into the explicit-root model

### Editor shell changes

- update Scene Tree rendering to draw the root node
- restore and save ImGui layout under `data/`
- restore and save native window geometry under `data/`

These responsibilities should remain separate so layout persistence does not
become entangled with scene serialization, and vice versa.

## Error Handling

- Missing local layout files are normal and must not surface as errors.
- Corrupt local layout files should fail soft:
  - ignore the broken layout
  - start with defaults
  - log a clear warning
- Legacy scene files without explicit root should load successfully through normalization.
- Invalid new-format scene files with malformed root structure should fail in-band
  with a clear load error.

## Verification Requirements

Automated coverage should include:

- `Scene` always exposes a real root node
- `/` resolves to that root node
- top-level authored nodes serialize and deserialize as children of the root
- legacy flat scene documents load and save back out in explicit-root form
- Scene Tree rendering logic includes the root row
- missing local layout files still allow clean startup
- local layout restore round-trips ImGui ini content
- native window state restore round-trips size, position, and maximized state

Manual verification should include:

- launch `lxe_editor`, confirm the Scene Tree shows `root`
- move panels, resize or maximize the main window, close the app, relaunch, and confirm layout restoration
- load an older scene file, save it, and confirm the new file now contains the explicit root structure
