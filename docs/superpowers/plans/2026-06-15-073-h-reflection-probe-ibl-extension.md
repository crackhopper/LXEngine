# 073-h Reflection Probe IBL Extension Plan

> Required sub-skill for implementation: use render-agent guardrails. This plan
> executes `REQ-073-h`.

## Tasks

- [ ] Add `ReflectionProbeComponent` parser/saver tests, including unknown-field
  rejection and camera-collection exclusion.
- [ ] Add graph-authored probe cubemap capture path and parser tests.
- [ ] Extend RenderWorkCompiler metadata for cubemap face capture work.
- [ ] Reuse the 073g bake pipeline for temporary probe EnvMap filtering.
- [ ] Add strict probe bake cache manifest loader/writer.
- [ ] Register live local probe resources in SceneResourceTable.
- [ ] Add runtime probe selection facts for Forward and Deferred.
- [ ] Add tiny Vulkan probe bake smoke.

## Verification

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_scene_bake_cache test_vulkan_probe_bake
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_scene_bake_cache)"
xvfb-run -a ./build/src/test/test_vulkan_probe_bake
rg -n "ReflectionProbe|reflectionProbe|IblBakeRenderer|bakeStaticEnvironment" src assets docs notes
```
