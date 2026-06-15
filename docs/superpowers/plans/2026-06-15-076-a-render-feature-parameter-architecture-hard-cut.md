# 076-a RenderFeature Parameter Architecture Hard Cut Plan

> Required sub-skill for implementation: use render-agent guardrails. This plan executes `REQ-076-a`.

## Tasks

- [ ] Extend `RenderFeatureParameter` with binding/member/required/range schema.
- [ ] Add parser tests for unknown fields, required field errors and typed
  binding/member declarations.
- [ ] Add feature-to-shader reflection validation in RenderWorkCompiler prepare.
- [ ] Add a runtime binding builder that consumes RenderFeature payloads and
  writes feature-owned UBO data into `RenderInputDesc.bindingPlan`.
- [ ] Split PostProcess `gamma` from `outputEncodingMode`.
- [ ] Remove or convert `createStandardPostProcessMaterial()` and
  `createSkyboxBackgroundMaterial()` so default paths do not hand-author feature
  UBO values.
- [ ] Update tone mapping, environment lighting and IBL lighting feature assets.
- [ ] Add cross-path rg audit for manual feature UBO writes.

## Verification

```bash
cmake --build build --target CompileShaders test_render_resource_parsers test_render_work_compiler test_shader_compiler test_lxe_editor_render_debug_dump lxe_editor
ctest --test-dir build --output-on-failure -R "(test_render_resource_parsers|test_render_work_compiler|test_shader_compiler|test_lxe_editor_render_debug_dump)"
rg -n "createStandardPostProcessMaterial|createSkyboxBackgroundMaterial|writeShaderBindingParameter\\(.*PostProcessUBO|bakeStaticEnvironment|IblBakeRenderer" src assets docs notes
```
