# Material-Driven Flat Shading And Mesh Debug Design

Date: 2026-05-19

## Purpose

Add flat shading and mesh-line visualization as first-class material capabilities
for LXEngine. The feature serves two goals:

- mesh structure debugging in `lxe_editor`
- the engine's long-term low-poly, pixel-art, and stylized rendering direction

The implementation must not preserve a second debug rendering path for surfaces
or wireframe display. Debug rendering must reuse the same material, render
queue, visibility, and pipeline mechanisms as ordinary renderables unless a
specific gap is documented for manual review.

## Current Context

The engine already has:

- reflection-driven `MaterialTemplate` / `MaterialInstance`
- pass-local shader variants stored on `ShaderProgramSet`
- render state in `MaterialPassDefinition`
- pipeline identity derived from shader variants, render state, object
  signature, and target signature
- visibility masks and `isDebugOnlyRenderable()` filtering semantics
- `debug_line` draw support, but debug line/surface behavior is currently a
  specialized path rather than a general material authoring capability

The design keeps normal `blinnphong` and `pbr` rendering free of debug-only
parameters.

## Design Summary

Flat shading and mesh-line display are material capabilities.

`shadingModel` is a high-level material field:

```yaml
shader: blinnphong_0
shadingModel: Flat
```

The material loader translates it into shader variants:

- `Smooth` maps to the existing shader behavior
- `Flat` enables `USE_FLAT_SHADING`

Mesh visualization is expressed by a dedicated debug material:

```yaml
shader: mesh_debug
shadingModel: Flat
meshOverlay:
  enabled: true
  color: [0.0, 0.0, 0.0, 1.0]
```

The debug material is an ordinary material asset/template. It can be assigned to
a real model for inspection. First version does not create proxy nodes or overlay
the original material automatically.

## Architecture

### Material Authoring

Add `shadingModel` to `.material` parsing:

- allowed values: `Smooth`, `Flat`
- missing field defaults to `Smooth`
- invalid values fail material loading with a clear error

Add `meshOverlay` to material parsing for materials that want explicit mesh-line
visualization:

- `enabled`: boolean, default `false`
- `color`: `Vec4`, default black opaque if enabled and omitted

`meshOverlay` is not an editor/debug-only setting. It is part of material
authoring so stylized assets can use the same mechanism as debug materials.

### Shader And Pipeline Identity

`shadingModel: Flat` appends an enabled `USE_FLAT_SHADING` shader variant to the
pass shader program. Because `ShaderProgramSet::getPipelineSignature()` already
includes enabled variants, smooth and flat materials naturally produce different
pipeline signatures.

First-version flat normals use shader derivatives:

```glsl
vec3 faceNormal = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
```

This allows any current mesh to be inspected without mesh-loader or asset-pipeline
changes.

Flat mode must ignore normal maps in first version. Tangent-space normal maps
and face-normal shading have conflicting semantics; a later stylized material can
explicitly design "flat base normal plus detail normal" if needed.

### Mesh-Line Visualization

Mesh lines must be rendered as a material-driven pass, not as a backend debug
special case.

First implementation derives a line-list edge buffer from triangle indices and
draws it with a dedicated `mesh_debug` shader/material pass. The pass is
represented by material/template/render-queue data, not by backend code checking
for debug objects.

Do not rely on Vulkan `VK_POLYGON_MODE_LINE` as the only path in first version:

- it requires non-solid fill mode device support
- it replaces filled surfaces instead of producing a colored overlay
- it does not naturally support "flat shaded surface plus fixed-color mesh
  lines"

`polygonMode: Line` can be added later as an optional backend capability if it is
useful, but it is not the primary implementation path.

### Debug Renderables

Debug objects are ordinary scene renderables with extra filtering semantics:

- `visibilityMask` decides which cameras can see them
- a debug-only flag allows dump/final-camera flows to exclude them even when the
  camera visibility mask would otherwise include them
- the debug flag must not decide how the object is shaded

How a debug object is drawn is controlled by its material. The default debug
material must show mesh lines and use flat shading for its filled surface.

### Inspecting A Real Model

First version supports material replacement:

- assign the debug material to a target model
- render it through the normal scene/render-queue/material path
- do not create shared-mesh proxy nodes yet
- do not automatically overlay the original material

Proxy-based workflows remain future work for cases where users want "original
material plus mesh debug overlay" simultaneously.

## Data Flow

Material file load:

1. Parse `shadingModel`.
2. Parse optional `meshOverlay`.
3. Translate `Flat` into `USE_FLAT_SHADING`.
4. Compile the requested shader family with variants.
5. Store the exact variant set on `MaterialPassDefinition::shaderProgram`.
6. Build the canonical material interface from reflection.

Runtime rendering:

1. Scene node exposes mesh, material, visibility mask, and debug-only flag.
2. Render queue filters by pass, camera visibility, and debug-only policy.
3. Render item carries the material-derived shader, render state, descriptors,
   and pipeline signature.
4. Backend builds/uses a pipeline from ordinary `PipelineBuildDesc`.

Backend code is not allowed to branch on "debug material" to draw differently.
Any such requirement is a design gap and must be documented.

## Error Handling

Material loading must fail clearly for:

- unknown `shadingModel`
- malformed `meshOverlay`
- `meshOverlay.color` that is not a Vec4-compatible value
- unsupported combinations that cannot be represented by the selected shader

Debug-only filtering must be explicit. A debug object excluded from dump/final
camera output must be excluded because render queue or camera policy filters
it, not because its material name is special.

## Testing

### Material Loader

- Missing `shadingModel` defaults to smooth and existing materials keep behavior.
- `shadingModel: Flat` produces an enabled `USE_FLAT_SHADING` variant.
- Invalid `shadingModel` reports a clear loader error.
- Debug material loads with `meshOverlay.enabled` and line color.

### Shader And Reflection

- Flat variant of the main forward shader compiles.
- Debug material shader compiles.
- Flat variant preserves required scene/material bindings.
- Mesh overlay color is available through material parameters or a reflected
  material-owned UBO.

### Render Queue And Pipeline

- Smooth and flat materials produce different material/pipeline signatures.
- Debug material produces ordinary render items and pipeline build descriptions.
- Debug-only renderables can be included/excluded through visibility/debug
  filtering without material-name checks.

### Code Review Checks

Implementation must include explicit code review passes for:

- stale debug surface/wireframe paths that bypass materials
- `blinnphong` / `pbr` pollution with debug-only parameters
- backend special cases for debug material names
- pipeline identity coverage for flat/mesh-overlay structure
- scene save behavior, especially temporary debug material assignment
- dump/final camera exclusion using visibility/debug policy

## Required Gap Document

If any logic cannot be unified under the material path during implementation,
create or update:

```text
notes/temp/material-debug-unification-gaps.md
```

Each entry must include:

- logic that cannot yet use the material path
- reason
- risk
- proposed follow-up

This document is required only when a gap exists. If implementation fully
unifies the path, the absence of the document is acceptable.

## Out Of Scope

- automatic shared-mesh debug proxy creation
- simultaneous original-material plus debug-overlay rendering
- full asset-pipeline flat-normal baking
- mandatory Vulkan polygon line mode
- toon ramp or quantized lighting beyond flat shading

## Future Work

- CPU/asset-side flat-normal mesh variants for low-performance platforms
- proxy-based debug overlays that preserve the original material
- optional backend polygon-line mode when device support is available
- stylized material families built on `shadingModel`, including toon and unlit
  pixel-art materials

## Acceptance Criteria

- A low-poly or stylized asset can use `shadingModel: Flat` as a permanent
  material choice.
- A model can be inspected by assigning a debug material that renders flat
  surface and fixed-color mesh lines.
- Debug objects remain ordinary renderables for visibility/render-queue purposes.
- Debug-only exclusion is controlled by visibility/debug policy, not material
  special cases.
- Normal `blinnphong` and `pbr` materials do not gain debug-only fields.
- Any unavoidable non-unified behavior is documented in
  `notes/temp/material-debug-unification-gaps.md`.
