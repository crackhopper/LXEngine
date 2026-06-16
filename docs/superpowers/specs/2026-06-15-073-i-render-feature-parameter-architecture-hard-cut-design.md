# 073-i RenderFeature Parameter Architecture Hard Cut Design

Date: 2026-06-15

## Goal

Close the architecture gap where shader UBO members are satisfied by C++ manual
binding writes instead of by RenderFeature parameters. This implements
`REQ-073-i`.

## Problem

`PostProcessUBO.gamma` exists in shader reflection and C++ static binding
metadata. Runtime writes it through `createStandardPostProcessMaterial()`.
That satisfies descriptor completeness but does not prove
`feature.toneMapping.gamma` was consumed.

The same failure mode can affect skybox, probe bake and IBL lighting.
Environment lighting has an extra boundary: the EnvMap URI and shader-visible
values both belong to `feature.environmentLighting`. Constant-color environment
is not a scene-side ambient fallback; it uses an explicit built-in EnvMap URI in
the environment feature and the same feature UBO as textured environments.

## Decision

RenderFeature parameter schema gains binding/member metadata:

```yaml
parameters:
  gamma:
    kind: float
    value: 2.2
    binding: PostProcessUBO
    member: gamma
    required: true
```

RenderWorkCompiler preparation validates:

```text
RenderPathGraph feature dependency
  -> live RenderFeature payload
  -> shader reflection binding/member
  -> RenderInputDesc binding plan
  -> backend descriptor write
```

Runtime-derived values must also be explicit. For PostProcess, target format
derives `outputEncodingMode`; it must not be hidden by changing `gamma`.

Environment follows the same rule:

```yaml
parameters:
  environmentMap:
    kind: textureCube
    uri: builtin:env/white_cube
    valueType: linear-radiance
    binding: SkyboxMap
    required: true
```

The URI above creates the live EnvMap resource. The same
`effects/environment_lighting.render-feature.yaml` file owns color, intensity,
rotation and visibility parameters, each with binding/member schema.
Any pass that declares `feature.environmentLighting` must include
`common/environment_lighting.glsl` or an equivalent common shader library so
reflection sees the same ABI that the feature YAML declares.

## Hard Cuts

- Default path cannot rely on manual `MaterialInstance` creation to carry
  feature UBOs.
- C++ hardcoded effect parameters are rejected except for explicitly declared
  runtime-derived schema fields.
- Placeholder feature payloads cannot satisfy graph dependencies.
- `scene.environment` cannot satisfy realtime environment dependencies or carry
  shader-visible values such as `ambientColor`, `ambientIntensity`,
  `intensity`, `rotation` or `uri`.
- Missing `feature.environmentLighting.parameters.environmentMap.uri` cannot
  create an implicit white cubemap.
- Built-in white cubemap for constant-color environment must be a live
  `SkyboxMap` payload, not a placeholder descriptor.

## Acceptance

- feature/schema/reflection mismatch rejects the input;
- PostProcess has separate `gamma` and `outputEncodingMode`;
- skybox, environment IBL, and probe feature parameters use the same validation path;
- scene-side environment fields are rejected or proved unable to satisfy the
  positive realtime graph;
- `builtin:env/white_cube` and HDR/KTX2 environments expose the same
  `SkyboxMap` binding path;
- rg audit proves manual PostProcess/Skybox feature binding is not the default
  positive path.
