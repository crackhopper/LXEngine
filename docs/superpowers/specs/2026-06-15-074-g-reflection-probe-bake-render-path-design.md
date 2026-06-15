# 074-g Reflection Probe And Bake Render Path Design

Date: 2026-06-15

## Goal

Replace private IBL/probe bake helpers with graph-authored reflection probe and
bake render paths. This implements `REQ-074-g`.

## Current Facts

- `IblBakeRenderer` hardcodes bake shader names and pass order.
- Existing bake shaders live at root names such as `ibl_prefilter_env` and
  `ibl_brdf_lut`.
- Current docs/specs described three stages; the active queue now treats probe
  capture, filtering and cache as one `REQ-074-g` slice.

## Decision

Add three graph-authored paths:

```text
ReflectionCapture  # probe camera -> capture.radiance cubemap
ReflectionFilter   # radiance cubemap -> PrefilteredEnvMap + DiffuseSH9
BrdfLutBake        # BSDF model -> BrdfLut
```

All graph resources are declared explicitly. Bake settings are RenderFeature or
graph settings, not C++ constants.

## Resource Flow

```text
ReflectionProbeComponent
  -> ReflectionCapture graph
  -> ReflectionFilter graph
  -> .lxe-bake/<scene-stem>/ manifest + payloads
  -> SceneResourceTable live ReflectionProbeBakeAsset / BrdfLutAsset
```

Metadata-only assets cannot satisfy `scene.reflectionProbes`.

## Required Rejections

- `renderPath: IBLBake`.
- root `ibl_*` shader URI as a positive graph shader.
- undeclared graph source/target.
- missing cubemap face/mip in bake cache.
- private `bakeStaticEnvironment()` as default path.

## Acceptance

- parser tests for probe component and graph assets;
- compiler tests for cubemap face/mip expansion;
- manifest loader/writer tests for strict cache schemas;
- Vulkan tiny bake smoke through graph executor;
- rg audit for `IblBakeRenderer|bakeStaticEnvironment|IBLBake|ibl_*` default path hits.
