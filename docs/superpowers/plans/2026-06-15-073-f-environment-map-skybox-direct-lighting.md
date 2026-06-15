# 073-f Environment Map Skybox Direct Lighting Plan

> Required sub-skill for implementation: use current repo facts only and apply render-agent guardrails. This plan is documentation for `REQ-073-f`; do not implement beyond the listed scope.

## Goal

Move environment-map skybox direct lighting onto RenderPathGraph +
RenderFeature + SceneResourceTable.

## Tasks

- [x] Add a color-only `scene.environment` fallback with
  `ambientColor`/`ambientIntensity`, parsed strictly and evaluated in
  Forward/Deferred lighting before postprocess.
- [ ] Add `effects/environment_lighting.render-feature.yaml` with typed
  `skyboxEnabled`, `intensity`, `rotation`, and `visibleInBackground`
  parameters.
- [ ] Move skybox shader URI under `render_paths/Skybox/` and update shader
  reflection tests.
- [ ] Add a Skybox pass to Forward/Deferred graph assets with
  `scene.environment` and `feature.environmentLighting` sources.
- [ ] Extend RenderFeature binding validation so skybox feature parameters must
  match reflected shader members.
- [ ] Route `SkyboxMap` through SceneResourceTable descriptor resources.
- [ ] Remove or hard-disable default positive use of
  `createSkyboxBackgroundMaterial()`.
- [ ] Add Vulkan smoke for enabled/disabled skybox and an rg audit for old
  manual helper paths.

## Verification

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_shader_compiler lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_shader_compiler)"
rg -n "createSkyboxBackgroundMaterial|shader: skybox|SkyboxMap" src assets docs notes
```
