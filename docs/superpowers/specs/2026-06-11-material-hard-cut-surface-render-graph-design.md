# Material Hard Cut: SurfaceMaterial And RenderPathGraph

## Context

`REQ-071-a` moved the default PBRT/PBR path toward pure `lxe.material.v2`
surface envelopes, but the repository still has old material-local rendering
truth:

- `MaterialTemplate` still owns pass, shader, render state and canonical shader
  bindings.
- `MaterialInstance` still has `ParameterBuffer` and shader-binding parameter
  APIs, even when envelope mode bypasses them.
- `GenericMaterialLoader` still treats non-v2 `.material` files as first-class
  material-local technique assets with `defaultTechnique`, `techniques`,
  `parameters` and `resources`.
- Several runtime/demo assets still contain `shader`, `defaultTechnique`,
  `techniques` and `MaterialUBO.*`.
- GPU material records still have fallback reads from old shader parameter
  buffers.

The user decision for this design is a hard cut:

- Keep only Helmet and BMW M6 runtime validation scenes.
- Delete old non-Helmet/non-BMW runtime/demo/test assets instead of preserving
  compatibility.
- Delete old material-local technique code before building the new structure, so
  new work cannot couple back into legacy paths.

## Goals

1. Make `.material` runtime assets pure `SurfaceMaterial` files.
2. Move shader, pass, render state, source/target and pass filtering to
   `RenderPathGraph`.
3. Remove `ParameterBuffer` as material instance truth.
4. Remove `GenericMaterialLoader` as a material-local technique loader.
5. Delete old assets and tests that depend on BlinnPhong/debug/RTR/material-local
   technique behavior.
6. Keep Helmet and BMW M6 as the only runtime scene acceptance targets.

## Non-Goals

- No compatibility mode for old `.material` files.
- No adapter layer that keeps old assets running.
- No attempt to preserve BlinnPhong/debug/RTR demo material behavior.
- No hidden fallback from missing graph/material data to old `MaterialUBO` data.

## Architecture

### SurfaceMaterial

`schema: lxe.material.v2` is the only valid runtime `.material` schema.

A surface material contains:

- PBRT `bsdf.type`.
- `bsdf.parameters` envelope values.
- canonical resource URIs.
- typed resource handles.
- dependency graph edges.
- optional render class and tags.

A surface material must not contain:

- shader URI.
- pass names.
- render state.
- `defaultTechnique` or `techniques`.
- root `parameters` or `resources`.
- legacy `MaterialUBO.*` or old PBR field names.

### RenderPathGraph

`RenderPathGraph` is the only asset that declares rendering structure:

- render path identity, such as Forward.
- pass ids.
- shader URIs.
- stage and dispatch.
- sources and targets.
- render state.
- filters by BSDF type, render class and feature support.

Helmet and BMW M6 should reference a minimal explicit graph. That graph may be
small at first, but it must be the source of shader/pass/render-state truth.

### SurfaceMaterialTemplate

The current `MaterialTemplate` should be reduced or replaced by a
`SurfaceMaterialTemplate` concept.

It may contain:

- BSDF type name.
- required and optional PBRT parameter schema.
- allowed envelope kinds per parameter.
- debug name or stable type id.

It must not contain:

- `MaterialPassDefinition`.
- shader programs.
- canonical shader bindings.
- pipeline signatures.
- render states.

### MaterialInstance

`MaterialInstance` is the runtime envelope instance.

It may contain:

- template/schema reference.
- BSDF type.
- envelope parameter table.
- typed texture/spectrum/bsdfTable/materialRef handles.
- dependency edges.
- render class and tags.
- dirty/version state.

It must not contain:

- `ParameterBuffer`.
- `setParameter(MaterialUBO, ...)` APIs.
- `readParameterValue` as a shader parameter source.
- pass enable state.
- pass shader, render state or pipeline signature accessors.

### GenericMaterialLoader

`GenericMaterialLoader` should stop being the old material-local technique
loader. Its only acceptable role is a thin wrapper over `MaterialResourceParser`
for `schema: lxe.material.v2`.

Rules:

- non-v2 `.material` files fatal.
- v2 material files are parsed with full root YAML by `MaterialResourceParser`.
- v2 files containing legacy root render-flow or parameter fields fatal.

### GPU Material Records

GPU material records are generated from surface envelopes and typed resource
handles only.

Allowed inputs:

- PBRT envelope constants.
- PBRT envelope texture handles.
- scene/object/render-class metadata needed by the graph.

Disallowed inputs:

- `MaterialUBO`.
- `SurfaceParams`.
- `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `ao`.
- generic shader parameter buffers.

## Migration Order

The order is part of the design:

1. Delete old assets and old behavior tests first.
2. Remove old material-local technique APIs from core types.
3. Let compile errors expose remaining old coupling.
4. Add the minimal new graph-driven path for Helmet and BMW M6.
5. Restore tests only around the new structure.

This prevents new work from accidentally writing back into old logic.

## Asset Policy

Only Helmet and BMW M6 runtime scene chains are preserved.

Delete or rebuild:

- `assets/materials/blinnphong_*`.
- `assets/materials/debug_line.material`.
- `assets/materials/mesh_debug.material`.
- `assets/materials/rtr_*`.
- `assets/materials/test_invalid_normal_*`.
- old shader sources and generated SPIR-V used only by those assets.
- scene/demo/editor/test entries that only exist to exercise those assets.

If a deleted asset exposes a code reference, delete or rewrite the reference for
Helmet/BMW/new graph coverage. Do not add placeholders.

## Testing Strategy

Delete or rewrite tests for old behavior:

- BlinnPhong material loading.
- debug material loading.
- RTR material loading.
- material-local variant rules.
- pass shader overrides inside `.material`.
- missing `defaultTechnique` / `techniques` old-format validation.
- root `parameters/resources` applied to `ParameterBuffer`.

Preserve or strengthen tests for new behavior:

- `MaterialResourceParser` v2 envelope contract.
- resource dependency, canonical URI, typed handle and dedup behavior.
- Helmet scene load with `Kd` and `normalmap` envelopes.
- BMW M6 scene load with pure v2 runtime materials.
- bindless upload view for material textures and GPU material records.
- `RenderPathGraph` parser/validator.
- non-v2 `.material` fatal.
- v2 material with legacy root render-flow fields fatal.
- source grep/audit that prevents `MaterialUBO` fallback from returning.

## Error Handling

Fail fast for authoring errors:

- `.material` without `schema: lxe.material.v2`.
- v2 material with shader/pass/render-state/root parameter fields.
- missing required PBRT parameters.
- graph pass missing shader, stage, dispatch, sources, targets or render state.
- graph shader requiring material data not available from the surface envelope.
- deleted asset still referenced by runtime code or tests.

Unsupported BSDF/render-class combinations should report a clear unsupported
diagnostic during graph validation and keep the object out of the render queue.
They must not synthesize old material parameter data.

## Success Criteria

- Build passes after old assets/tests are removed and the Helmet/BMW graph path
  is restored.
- Runtime acceptance targets are Helmet and BMW M6 only.
- `.material` runtime files are pure `lxe.material.v2`.
- `MaterialTemplate` no longer owns pass/shader/render-state data.
- `MaterialInstance` no longer owns parameter buffers or pass state.
- `GenericMaterialLoader` is not a legacy material-local technique parser.
- GPU material records no longer read old parameter-buffer fallback data.
- Grep/audit tests prevent reintroducing material-local technique and
  `MaterialUBO` truth in runtime paths.
