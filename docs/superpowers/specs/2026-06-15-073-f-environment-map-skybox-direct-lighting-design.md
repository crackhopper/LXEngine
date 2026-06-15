# 073-f Environment Map Skybox Direct Lighting Design

Date: 2026-06-15

## Goal

Make environment-map skybox/direct background lighting a graph-authored render
path feature, not a backend helper that manually creates a fullscreen material.

This implements `REQ-073-f`.

## Current Facts

- `SceneDocument` already parses `scene.environment`.
- `SceneRuntime` loads HDR environment data and prepares `SkyboxMap`.
- `skybox.vert/.frag` exist, but the positive Vulkan path still goes through
  `VulkanPostProcessBuilder::createSkyboxBackgroundMaterial()`.
- Environment and skybox parameters are not yet validated through a
  RenderFeature-to-shader-binding contract.

## Decision

Forward and Deferred graph assets declare an explicit skybox/background pass:

```yaml
features:
  - id: environmentLighting
    uri: effects/environment_lighting.render-feature.yaml

passes:
  - id: Skybox
    stage: raster
    dispatch: fullscreen
    shader: render_paths/Skybox/skybox_background
    input:
      kind: fullscreen-triangle
    sources: [scene.environment, feature.environmentLighting]
    targets: [scene.hdrColor]
```

The shader consumes:

- `SkyboxMap` from SceneResourceTable scene-level resources;
- feature-owned environment parameters from `feature.environmentLighting`.

Surface/material parameters remain in material source contracts. Environment
visibility, intensity and rotation are render-feature parameters.

Before cubemap irradiance is fully graph-authored, `scene.environment` may also
carry a color-only fallback:

```yaml
environment:
  enabled: false
  ambientColor: [0.08, 0.08, 0.10]
  ambientIntensity: 0.7
  skyboxEnabled: false
```

This fallback is evaluated in Forward/Deferred lighting before postprocess. It
is not an exposure change and not a postprocess color add. Diffuse contribution
is material-aware (`baseColor`, `metallic`, `ao`) and the specular term is a
small roughness/Fresnel approximation until cubemap IBL replaces it.

## Required Rejections

- Graph missing `scene.environment` while skybox pass is enabled.
- Feature missing required parameter such as `intensity`.
- Shader requiring `SkyboxMap` but descriptor plan has no live payload.
- Default path calling manual skybox material injection.

## Acceptance

- Parser tests cover the skybox pass and environment feature dependency.
- RenderWorkCompiler produces a fullscreen typed input and accepted desc.
- Vulkan smoke proves skybox appears when enabled and disappears when disabled.
- rg audit has no default positive-path hit for manual skybox material injection.
