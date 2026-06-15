# 075-a IBL Lighting Post Effect Design

Date: 2026-06-15

## Goal

Make Forward and DeferredLighting consume global/local IBL resources through a
single RenderFeature-backed lighting effect. This implements `REQ-075-a`.

## Terminology

“Post effect” here means a lighting effect applied after material surface
parameters are known. It is not the final screen-space PostProcess pass.

## Decision

Add `feature.iblLighting`:

```yaml
schema: lxe.render-feature.v1
name: IblLighting
feature: iblLighting
parameters:
  enabled: { kind: bool, value: true }
  diffuseIntensity: { kind: float, value: 1.0 }
  specularIntensity: { kind: float, value: 1.0 }
  maxProbeCount: { kind: int, value: 8 }
  fallbackMode: { kind: enum, value: zero }
```

Forward and DeferredLighting declare:

```yaml
sources:
  - scene.reflectionProbes
  - scene.brdfLuts
  - feature.iblLighting
```

The shaders include `common/environment_lighting.glsl` and use the same binding
contract.

## Ownership

- Material owns albedo, roughness, metallic, normal and texture slots.
- RenderFeature owns IBL intensities, fallback mode and probe limits.
- SceneResourceTable owns live probe, SH, prefiltered cubemap and BRDF LUT
  payloads.

## Required Rejections

- shader requires probe resources but graph omits sources;
- feature parameter missing/wrong type;
- metadata-only probe payload;
- backend hardcoded IBL intensity/mip count in positive path.

## Acceptance

- zero probe set returns explicit zero contribution or configured failure;
- valid global probe visibly changes PBR shading;
- Forward and DeferredLighting share common environment lighting include;
- shader reflection and RenderInputDesc binding plan agree.
