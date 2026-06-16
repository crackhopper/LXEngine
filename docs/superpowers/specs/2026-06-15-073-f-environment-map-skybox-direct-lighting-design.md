# 073-f Environment Map Skybox Direct Lighting Design

Date: 2026-06-15

## Goal

Make environment-map skybox/direct background lighting a graph-authored render
path feature, not a backend helper that manually creates a fullscreen material.
Texture environments and constant-color environments use one
RenderFeature-owned EnvMap resource path.

This implements `REQ-073-f`.

## Current Facts

- `SceneDocument` already parses legacy `scene.environment`.
- `SceneRuntime` currently loads HDR environment data from that scene field and
  prepares `SkyboxMap`.
- `skybox.vert/.frag` exist, but the positive Vulkan path still goes through
  `VulkanPostProcessBuilder::createSkyboxBackgroundMaterial()`.
- Environment and skybox parameters are not yet validated through a
  RenderFeature-to-shader-binding contract.
- Current scene files may carry temporary `ambientColor` / `ambientIntensity`
  fields. The target design removes that scene-side environment authoring path
  from the positive realtime graph.

## Decision

Forward and Deferred graph assets declare an explicit skybox/background pass.
This pass is not a scene object and does not introduce a skybox material
category. It is a graph-authored background effect with fullscreen input:

```yaml
features:
  environmentLighting:
    uri: effects/environment_lighting.render-feature.yaml

passes:
  - id: Skybox
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Skybox/skybox_background
    input:
      kind: fullscreen-triangle
    sources: [feature.environmentLighting, depth.main]
    targets: [hdr.color]
```

The shader consumes:

- `SkyboxMap` registered from
  `feature.environmentLighting.parameters.environmentMap.uri`;
- feature-owned environment shader parameters from
  `feature.environmentLighting`.

Surface/material parameters remain in material source contracts. Environment
visibility, color, intensity and rotation are render-feature parameters.

The environment feature owns both the EnvMap resource URI and shader-visible
values:

```yaml
schema: lxe.render-feature.v1
name: EnvironmentLighting
feature: environmentLighting
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
  color:
    kind: vec3
    value: [0.08, 0.08, 0.10]
    binding: EnvironmentLightingUBO
    member: color
    required: true
  intensity:
    kind: float
    value: 1.0
    binding: EnvironmentLightingUBO
    member: intensity
    required: true
  rotation:
    kind: float
    value: 0.0
    binding: EnvironmentLightingUBO
    member: rotation
    required: true
  visibleInBackground:
    kind: bool
    value: true
    binding: EnvironmentLightingUBO
    member: visibleInBackground
    required: true
```

Constant-color environment is represented by `environmentMap.uri:
builtin:env/white_cube`. The resource loader registers a live 1x1 white cubemap
as `SkyboxMap`; the shader samples it exactly like any other cubemap and
multiplies by feature-owned color / intensity. Missing `environmentMap.uri`
means no environment, not an implicit white cubemap.

Pass shaders that depend on `feature.environmentLighting` include
`common/environment_lighting.glsl` or an equivalent common shader library. Build
and prepare validation prove the feature YAML resource binding and UBO
binding/member declarations match shader reflection.

`color` and `intensity` define the EnvMap source radiance. The visible
background pass uses them directly, and `REQ-073-g` reuses the same values for
Forward/Deferred surface lighting before applying `feature.surfaceLighting`
diffuse/specular contribution multipliers.

`SkyboxBackground` writes visible environment color to `hdr.color` only where
geometry has not covered the pixel. Surface lighting is deliberately outside
this slice. Forward and DeferredLighting will consume the same EnvMap resource
and feature ABI in `REQ-073-g`; this design only prepares the resource and
feature contract they will reuse.

Depth participation is graph-authored. `Skybox` needs `depth.main` as a
read-only depth attachment for fixed-function depth testing, not as a sampled
descriptor and not as a depth write. The RenderPathGraph attachment / target
contract therefore gains an explicit `attachmentUsage` field:

```yaml
rendering:
  mode: dynamic
  attachments:
    - target: hdr.color
      format: RGBA16Float
      samples: 1
      layers: 1
      attachmentUsage: color-attachment-write
    - target: depth.main
      format: D32Float
      samples: 1
      layers: 1
      depth: true
      attachmentUsage: depth-attachment-read-only
```

FrameGraph and backend consume this schema fact. They must not infer the depth
attachment from pass name, shader name or C++ temporary logic. `depth.main`
appears in `sources` to model the dependency, but it does not appear in
`targets` for this pass.

The rule is general, not skybox-specific. A pass may have the same graph
resource in both `sources` and `targets` only when the attachment usage is
explicitly read-write, for example `depth-attachment-read-write`. FrameGraph
build and validation must understand dependency read, attachment read-only,
attachment write and attachment read-write as separate facts instead of treating
same-name source/target pairs as an automatic error.

## Required Rejections

- Graph missing `feature.environmentLighting` while skybox pass is enabled.
- Environment feature missing `environmentMap.uri`.
- Feature missing required parameter such as `intensity`.
- Shader requiring `SkyboxMap` but descriptor plan has no live payload.
- Scene environment fields such as `ambientColor`, `ambientIntensity`,
  `intensity`, `rotation` or `uri` satisfying the positive realtime graph.
- Missing `environmentMap.uri` silently creating a default white cubemap.
- `Skybox` declaring `depth.main` as a writable target to get depth testing.
- Backend pass-name special casing that binds depth outside RenderPathGraph
  schema.
- Default path calling manual skybox material injection.
- Treating the background pass as surface IBL/BRDF lighting.

## Acceptance

- Parser tests cover the skybox pass and environment feature dependency.
- Parser/FrameGraph tests cover `attachmentUsage`, read-only depth attachment
  access, legal read-write same-resource usage, and accidental duplicate depth
  writes.
- RenderWorkCompiler produces a fullscreen typed input and accepted desc.
- RenderFeature resource loader tests prove `environmentMap.uri:
  builtin:env/white_cube` creates a live `SkyboxMap` and missing URI does not.
- Vulkan smoke proves constant-color skybox appears when enabled and disappears
  when disabled.
- Forward/Deferred surface lighting is documented as `REQ-073-g` ownership, not
  a 073-f acceptance condition.
- rg audit has no default positive-path hit for manual skybox material injection.
