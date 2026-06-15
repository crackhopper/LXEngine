# REQ-073-c Material Source Variant Pipeline Identity Design

Date: 2026-06-14

## Decision

`REQ-073-c` establishes the shader and pipeline identity boundary between the
source-reflected material records from `REQ-073-b` and the later indirect /
hard-cut renderer work in `REQ-073-e/f`.

The core design is:

```text
PipelineKey = MaterialTypeVariant + RenderPathNodeSignature
```

Material `type` owns shader variant selection. `source` is the checked shader
contract attached to that type, not a second material classifier. A scene may
have many material instances of the same type, but the same type must resolve
to one source URI, one reflection hash, and one source signature inside that
scene.

`standard-pbr` is a new material type for glTF metallic-roughness assets. It is
not a PBRT `uber` parameter extension, because metallic-roughness changes the
surface model rather than only adding a field to an existing PBRT material.

## Scope

This design includes:

- material type to source variant validation;
- `standard-pbr` contract and Helmet/glTF conversion;
- shader-side BSDF ABI for pass shaders;
- final material-source shader variant compilation and reflection;
- RenderPathNode pipeline contract and node signature;
- `PipelineKey` hard cut to material type variant plus RenderPathNode signature;
- dynamic/traditional rendering mode as explicit RenderPathNode data;
- variant-aware shader build targets;
- converted Helmet realtime smoke that must not pass through old material paths.

This design intentionally excludes:

- `techniques/...` to `render_paths/...` URI migration, owned by `REQ-073-d`;
- indirect draw batching and batch split diagnostics, owned by `REQ-073-e`;
- deleting all realtime fallback paths, owned by `REQ-076-b`;
- OfflineRT config and entry hard cuts, owned by `REQ-076-c/d`.

The excluded work is not a workaround permission. The 073-c positive path must
not rely on old fallback behavior to pass tests or smoke.

## Material Identity

`bsdf.type` is the semantic material family used by RenderPath filters and
shader variant resolution. `bsdf.source` points to the shader contract source
that defines storage fields, accessors, and BSDF functions for that type.

Validation rules:

- `.material` `bsdf.type` must match the reflected source type.
- One scene/resource table must not map the same `bsdf.type` to multiple source
  URIs, reflection hashes, or source signatures.
- Material instance values, texture handles, texture presence, material URI, and
  material name do not participate in material type variant identity.
- A conflict that is "impossible by design" is still a bug and must diagnose
  the conflicting materials. The implementation must not invent a layout
  workaround for it.

`sourceStorageIndex + sourceLocalMaterialIndex` remains the storage reference
shape from `REQ-073-b`. `073-c` adds the shader variant identity that consumes
that storage, not another material storage key.

## Standard PBR

`standard-pbr` represents the glTF metallic-roughness workflow.

Its contract source must expose at least:

- base color factor and base color texture;
- metallic factor and metallic-roughness texture;
- roughness factor;
- normal texture;
- occlusion texture;
- emissive factor and emissive texture;
- alpha mode and alpha cutoff.

Helmet conversion must emit explicit Material v2 assets with:

```yaml
bsdf:
  type: standard-pbr
  source: assets://shaders/glsl/common/materials/standard_pbr.contract.glsl
```

The converted scene may continue to reference the glTF mesh, but it must not use
`assets/materials/pbr.material` or a runtime `source: gltf` bridge to prove this
requirement.

## Material Converter

The Helmet/glTF converter is part of `REQ-073-c`, not a later smoke-only
cleanup item.

The converter must:

- read glTF metallic-roughness material data;
- preserve base color factor/texture, metallic factor, roughness factor,
  metallic-roughness texture, normal texture, occlusion texture, emissive
  factor/texture, alpha mode, and alpha cutoff;
- emit a `standard-pbr` Material v2 file with explicit `bsdf.type` and
  `bsdf.source`;
- emit a scene YAML that references the generated material and the Helmet mesh;
- avoid `assets/materials/pbr.material`, `type=uber`, and runtime `source: gltf`
  as success paths;
- have a test that asserts the generated material/scene contain the clean path
  fields and omit old path strings.

This converter is the bridge from real glTF input data to the new material
source variant architecture. Helmet smoke consumes the converted asset.

## Shader ABI

Every material source that can be used by lighting passes must implement a
common BSDF ABI. Pass shaders include one material source variant and call the
same functions regardless of the material type.

Required functions:

```glsl
LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput input);
LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput input);
```

`lxEvaluateBsdf` evaluates a fixed pair of directions. The caller supplies both
sides of the scattering event, and the material source returns the BSDF value
`f(wi, wo)` for that pair.

`lxSampleBsdf` samples the missing direction from the supplied incident/outgoing
side and random sample. It returns the sampled direction, the BSDF value for the
sampled pair, and the sampling PDF for that sampled direction. There is no
required standalone `lxPdfBsdf` entry in this requirement.

Forward and Deferred lighting need evaluation. OfflineRT direct lighting needs
evaluation and sampling. `LxMaterialSurface` can remain as an accessor for
GBuffer/debug-style data, but it is not the primary lighting interface.

Pass shaders must not contain material type/source runtime branches, and they
must not emulate polymorphism with large `#if MATERIAL_TYPE_*` trees. The
selected source variant provides the functions.

## RenderPathNode Signature

RenderPathNode is the owner of pass/pipeline structure. Material identity does
not own pass shader, render state, or attachment data.

The node signature includes:

- pass id;
- resolved base shader identity;
- stage and dispatch;
- render state;
- source and target resource declarations;
- geometry vertex/topology contract;
- rendering mode;
- attachment contracts required by the selected rendering mode.

The node parser must reject unknown fields and unknown resource vocabulary. It
should centralize built-in names such as `geometry.vertex`, `geometry.index`,
`material.bsdf`, `scene.camera`, `scene.lights`, `hdr.color`, `depth.main`,
`swapchain.color`, and the GBuffer targets.

Object vertex/topology facts are validated against the node geometry contract.
They are not a separate PipelineKey axis. If an object is incompatible with the
node contract, scene preparation fails with a diagnostic instead of creating a
new pipeline key.

## Rendering Mode

Rendering mode is explicit node data:

- `dynamic`: Vulkan dynamic rendering path.
- `traditional`: traditional render pass / framebuffer compatibility path.

Both modes still need attachment contracts when raster outputs are produced.
For dynamic Vulkan graphics pipeline creation, color/depth/stencil formats are
provided through dynamic rendering pipeline state rather than a traditional
render pass object. Those formats still affect pipeline compatibility, so they
belong inside `RenderPathNodeSignature`.

The backend must not auto-switch modes. If a device or backend cannot support
the declared mode, it fails with an unsupported rendering mode diagnostic.

## Variant Resolution Data Flow

Scene preparation resolves shader variants after resource registration and
before RenderWorkQueue / PipelineBuildDesc construction:

```text
Material resources
  -> type/source uniqueness validation
  -> material type variant registry
RenderPathGraph nodes
  -> RenderPathNodeSignature
  -> required material types from filters and scene usage
MaterialTypeVariant + RenderPathNodeSignature
  -> compile final shader variant with LX_MATERIAL_CONTRACT_SOURCE
  -> reflect final shader
  -> attach final shader identity/reflection to renderable pass data
```

The final shader variant is the only reflection source for descriptor layout,
push constants, vertex inputs, and pipeline build data. Base shader reflection
is not allowed to stand in for variant reflection on source-variant passes.

## Pipeline Key

The public pipeline key builder should expose the new shape directly:

```cpp
PipelineKey::build(materialTypeVariant, renderPathNodeSignature);
```

Old object/material/target overloads should be removed from positive paths
rather than forwarded. Tests that still assert `ObjectRender` or `TargetRender`
inside the debug string are old-contract tests and must be updated as part of
the hard cut.

Examples:

- Same `standard-pbr` type, same Forward node, different base color texture:
  same PipelineKey.
- Same `standard-pbr` type, same Forward node, different roughness value:
  same PipelineKey.
- `standard-pbr` vs PBRT `uber` on the same Forward node: different PipelineKey.
- Same material type on Forward node vs GBuffer node: different PipelineKey.
- Same node id but different render state or attachment contract: different
  RenderPathNodeSignature, therefore different PipelineKey.

## Shader Build Boundary

Shaders that require `LX_MATERIAL_CONTRACT_SOURCE` are variant-only base
sources. They should be copied as runtime sources, but they must not be treated
as successful naked compile units.

Build/test structure:

- normal shader target compiles shaders that do not require material source;
- material source variant target compiles Forward, Deferred, and OfflineRT
  source-variant shaders with at least `standard-pbr`;
- missing source variant context fails with diagnostics that name the base
  shader path, source URI or missing macro, and expected variant build route.

Deleting shader-side `#error`, injecting an empty source, or falling back to old
`MaterialUBO` is not acceptable.

## Diagnostics

Preparation and smoke diagnostics must expose the identities needed to explain
failure:

- material URI, type, source URI, reflection hash, source signature;
- base shader identity;
- RenderPathGraph and RenderPathNode id;
- RenderPathNodeSignature;
- compile key and reflection key;
- PipelineKey;
- rendering mode and attachment contract summary;
- geometry/topology compatibility failures;
- final shader binding summary.

When rendering cannot be prepared, the correct behavior is to stop and report
the missing contract. Hidden fallback is a test failure.

## Testing Strategy

The first implementation step should add characterization tests that fail
against the current code:

- `PipelineKey` ignores object and independent target axes.
- raster draw nodes require explicit `rendering` and `geometry`.
- unknown RenderPath resource names fail.
- one material type cannot map to multiple sources.
- `standard-pbr` contract reflects required fields.
- variant-only shaders fail naked compile with a source-variant diagnostic.

Additional tests then cover:

- glTF material factor extraction and `standard-pbr` material generation;
- BSDF ABI reflection and negative missing-function cases;
- variant-aware shader build target;
- final shader reflection consumption in PipelineBuildDesc;
- dynamic/traditional rendering mode pipeline data;
- Helmet converter output;
- low-resolution Helmet realtime smoke with non-black image statistics and
  logs proving the new path was used.

## Acceptance Gate

`REQ-073-c` is complete only when:

- focused CPU tests for material contract, RenderPath parser, shader compiler,
  pipeline identity, and pipeline build info pass;
- variant shader build target passes;
- notes build passes and shows this spec plus the implementation plan;
- Helmet converted `standard-pbr` scene runs realtime smoke to a non-black
  output without old material fallback.

If Helmet smoke cannot render, implementation stops at the first preparation or
pipeline diagnostic and updates the requirement status with the exact blocker.
