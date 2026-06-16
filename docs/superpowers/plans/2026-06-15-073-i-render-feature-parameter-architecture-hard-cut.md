# 073-i RenderFeature Parameter Architecture Hard Cut Plan

> Required sub-skill for implementation: use render-agent guardrails. This plan executes `REQ-073-i`.

## Tasks

- [ ] Extend `RenderFeatureParameter` with binding/member/required/range schema.
- [ ] Add parser tests for unknown fields, required field errors and typed
  binding/member declarations.
- [ ] Add feature-to-shader reflection validation in RenderWorkCompiler prepare.
- [ ] Add a runtime binding builder that consumes RenderFeature payloads and
  writes feature-owned UBO data into `RenderInputDesc.bindingPlan`.
- [ ] Split PostProcess `gamma` from `outputEncodingMode`.
- [ ] Hard-cut environment shader parameter ownership: reject scene-side
  `ambientColor`/`ambientIntensity`/`environment.uri`/shader controls, require
  explicit `feature.environmentLighting.parameters.environmentMap.uri`, and
  keep constant-color environment on `builtin:env/white_cube` + feature-owned
  color/intensity.
- [ ] Require environment-consuming pass shaders to include
  `common/environment_lighting.glsl` or an equivalent common shader ABI that
  matches `effects/environment_lighting.render-feature.yaml`.
- [ ] Remove or convert `createStandardPostProcessMaterial()` and
  `createSkyboxBackgroundMaterial()` so default paths do not hand-author feature
  UBO values.
- [ ] Update tone mapping, environment lighting and IBL lighting feature assets.
- [ ] Add cross-path rg audit for manual feature UBO writes.

## Verification

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_shader_compiler test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_shader_compiler|test_lxe_editor_render_debug_dump)"
rg -n "createStandardPostProcessMaterial|createSkyboxBackgroundMaterial|writeShaderBindingParameter\\(.*PostProcessUBO|ambientColor|ambientIntensity|bakeStaticEnvironment|IblBakeRenderer" src assets docs notes
```
