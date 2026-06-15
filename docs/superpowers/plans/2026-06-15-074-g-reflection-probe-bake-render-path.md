# 074-g Reflection Probe And Bake Render Path Plan

> Required sub-skill for implementation: use render-agent guardrails. This plan executes `REQ-074-g`.

## Tasks

- [ ] Add `ReflectionProbeComponent` parser/saver tests, including unknown-field
  rejection and camera-collection exclusion.
- [ ] Add strict `ReflectionCapture`, `ReflectionFilter`, and `BrdfLutBake`
  RenderPathGraph parser tests.
- [ ] Move bake shaders under `assets/shaders/glsl/render_paths/` and reject
  root `ibl_*` positive shader URIs.
- [ ] Extend RenderWorkCompiler metadata for cubemap face/mip bake iterations.
- [ ] Add graph assets for capture, filter and BRDF LUT bake.
- [ ] Add strict scene bake cache manifest loader/writer.
- [ ] Register live probe/LUT payloads in SceneResourceTable.
- [ ] Replace private `IblBakeRenderer` default entry with graph executor or
  hard-disable it as a positive path.

## Verification

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_scene_bake_cache test_vulkan_ibl_bake
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_scene_bake_cache)"
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
rg -n "IBLBake|IblBakeRenderer|bakeStaticEnvironment|ibl_prefilter_env|ibl_brdf_lut" src assets docs notes
```
