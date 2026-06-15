# 075-a IBL Lighting Post Effect Plan

> Required sub-skill for implementation: use render-agent guardrails. This plan executes `REQ-075-a`.

## Tasks

- [ ] Add `effects/ibl_lighting.render-feature.yaml` and parser tests for typed
  parameters.
- [ ] Add `assets/shaders/glsl/common/environment_lighting.glsl`.
- [ ] Update Forward and DeferredLighting shader reflection tests for shared IBL
  bindings.
- [ ] Update Forward/Deferred graph assets to declare
  `scene.reflectionProbes`, `scene.brdfLuts`, and `feature.iblLighting`.
- [ ] Add SceneResourceTable live probe-set and BRDF LUT descriptor resources.
- [ ] Add negative tests for metadata-only probe payloads.
- [ ] Add Vulkan smoke for zero-probe and valid global-probe lighting.
- [ ] Audit hardcoded backend IBL intensity/probe count paths.

## Verification

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_scene_resource_upload_view_v2 test_shader_compiler lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_scene_resource_upload_view_v2|test_shader_compiler)"
rg -n "iblIntensity|maxProbeCount|EnvironmentUBO|scene.reflectionProbes|feature.iblLighting" src assets docs notes
```
