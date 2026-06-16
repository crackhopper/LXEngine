# 073-h IBL Lighting Post Effect Design

Date: 2026-06-15

## Goal

Make Forward and DeferredLighting consume global/local IBL resources through a
single RenderFeature-backed lighting effect. This implements `REQ-073-h`.
It owns surface lighting for constant-color EnvMap, global cubemap EnvMap and
reflection probes.

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
  constantDiffuseIntensity: { kind: float, value: 1.0 }
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
  - feature.environmentLighting
  - feature.iblLighting
```

The shaders include `common/environment_lighting.glsl` and use the same binding
contract. Constant-color lighting uses the same EnvMap source created by
`REQ-073-f`: `feature.environmentLighting.parameters.environmentMap.uri:
builtin:env/white_cube` produces `SkyboxMap`, and feature values turn that
white sample into radiance. There is no separate ambient fallback in Forward or
Deferred.

`feature.environmentLighting.color` / `intensity` define EnvMap source radiance
and are shared with the visible background pass. `feature.iblLighting`
diffuse/specular intensities are contribution multipliers applied during
surface lighting; they do not redefine the EnvMap source.

## Ownership

- Material owns albedo, roughness, metallic, normal and texture slots.
- RenderFeature owns IBL intensities, fallback mode and probe limits.
- SceneResourceTable owns live global EnvMap loaded from
  `feature.environmentLighting`, plus probe, SH, prefiltered cubemap and BRDF
  LUT payloads.

## Required Rejections

- shader requires probe resources but graph omits sources;
- feature parameter missing/wrong type;
- metadata-only probe payload;
- Forward/Deferred retaining a separate constant-color ambient path;
- backend hardcoded IBL intensity/mip count in positive path.

## Acceptance

- zero environment/probe set returns explicit zero contribution or configured
  failure;
- `builtin:env/white_cube` constantColor and valid global probe visibly change
  PBR shading through the shared lighting path;
- Forward and DeferredLighting share common environment lighting include;
- shader reflection and RenderInputDesc binding plan agree.
