# REQ-071a Close Default Material Truth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close REQ-071-a by removing old material truth from the default/runtime path: default assets, generated BMW/helmet materials, GPU material records, and generic loading must use `lxe.material.v2` PBRT envelopes instead of root `parameters/resources`, `MaterialUBO.*`, or legacy PBR field names.

**Architecture:** Runtime PBRT materials store only `schema: lxe.material.v2` plus `bsdf.parameters` envelopes. `MaterialResourceParser` is the only parser that accepts v2 parameter truth; `GenericMaterialLoader` routes every v2 file through that parser with the full YAML root, so v2 assets cannot carry transitional technique/pass/root shader fields. GPU upload derives `SceneGpuMaterialRecord` and bindless texture slots from envelopes and handles, not from `baseColorFactor/metallicFactor/roughnessFactor` fallback.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, Python PBRT converter tests, LXEngine material/parser/upload integration tests.

---

## Current Gap

The latest implementation removed `MaterialInstance`'s envelope + GPU parameter buffer dual state, but REQ-071-a still has default-path legacy truth:

- `assets/materials/pbr.material` and `assets/materials/pbr_gold.material` still contain root `parameters:` / `resources:` and `MaterialUBO.*`.
- Several default runtime materials still use root `parameters:` and old shader/variant fields. For non-PBRT custom materials this can remain only if they are explicitly treated as legacy/custom, but PBR/default runtime assets must be v2.
- `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py` emits mix material refs as `uri: named:<name>`, not as real `.material` URIs.
- `src/core/scene/scene_gpu_records.cpp` still reads `baseColorFactor`, `metallicFactor`, `roughnessFactor`, and `ao` fallback.
- `src/infra/material_loader/generic_material_loader.cpp` still applies root `parameters:` and root `resources:` on non-v2/default material files.
- Current red lights to explicitly address in this pass: `test_material_instance`, `test_scene_resource_table`, `test_generic_material_loader`.

## File Structure

- Modify: `assets/materials/pbr.material`
  - Remove root `parameters:` / `resources:` and keep PBRT envelope truth only.
- Modify: `assets/materials/pbr_gold.material`
  - Convert from old PBR root parameters into `schema: lxe.material.v2`.
- Review and possibly modify: `assets/materials/*.material`
  - Classify remaining root `parameters:` materials as either custom legacy test/demo materials or migrate them if they are default/runtime PBR assets.
- Modify: `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py`
  - Resolve mix `materialRef` envelopes to generated runtime `.material` URIs.
- Modify: `src/test/integration/test_lxe_pbrt_scene_convert.py`
  - Assert mix runtime material contains `kind: materialRef` with real `.material` URI and no `named:` URI.
- Modify: `src/core/scene/scene_gpu_records.cpp`
  - Remove old PBR fallback and expand PBRT envelope-to-GPU-record mapping.
- Modify: `src/test/integration/test_scene_resource_table.cpp`
  - Strengthen upload-view tests for envelope-only GPU material record generation.
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
  - Reject legacy root PBR parameter keys for default/runtime PBR materials, and reject root `resources:` on v2/default PBR assets.
- Modify: `src/test/integration/test_generic_material_loader.cpp`
  - Update pbr/pbr_gold tests from legacy `MaterialUBO` assertions to v2 envelope assertions.
- Modify: `src/test/integration/test_material_instance.cpp`
  - Keep the envelope-only regression test independent of missing shader source and ensure no v2 parameter buffer appears.
- Modify: `src/test/integration/test_scene_runtime.cpp`
  - Update IBL/helmet/default runtime assertions to inspect v2 envelopes or upload records instead of `MaterialUBO.*`.
- Modify: `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`
  - Record actual implementation status and remaining out-of-scope legacy/custom materials.

## Task 0: Baseline And Asset Audit

**Files:**
- Modify: none

- [x] **Step 1: Inspect relevant worktree state**

Run:

```bash
git status --short assets/materials src/core/scene src/infra/material_loader src/tools/lxe_pbrt_scene_convert src/test/integration
```

Expected: Existing unrelated dirty worktree is visible. Do not revert unrelated changes.

- [x] **Step 2: List legacy material fields in default assets**

Run:

```bash
rg -n "^(parameters|resources):|MaterialUBO\\.|baseColorFactor|metallicFactor|roughnessFactor|^shader:|^variants:" assets/materials
```

Expected: `pbr.material` and `pbr_gold.material` are in the must-migrate set. BlinnPhong/RTR/debug files may still appear; classify them as custom legacy materials unless they are used as default PBR/runtime path.

- [x] **Step 3: Capture current red lights**

Run:

```bash
cmake --build build --target test_material_instance test_scene_resource_table test_generic_material_loader
ctest --test-dir build --output-on-failure -R 'test_(material_instance|scene_resource_table|generic_material_loader)'
```

Expected: Record exact failures. If failures are only missing `assets/shaders/glsl/techniques/Forward/*.vert|frag`, keep implementing fake/parser-driven tests first.

## Task 1: Fix PBRT Converter Mix MaterialRef URI

**Files:**
- Modify: `src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py`
- Modify: `src/test/integration/test_lxe_pbrt_scene_convert.py`

- [x] **Step 1: Add failing converter assertions**

In `test_lxe_pbrt_scene_convert.py`, after reading the generated runtime `LEATHER.material`, add:

```python
runtime_mix_text = (
    out_root
    / "materials"
    / "runtime-pbr-approx"
    / "LEATHER.material"
).read_text(encoding="utf-8")
self.assertIn('"type": "mix"', runtime_mix_text)
self.assertIn('"namedmaterial1":', runtime_mix_text)
self.assertIn('"kind": "materialRef"', runtime_mix_text)
self.assertIn(
    '"uri": "materials/runtime-pbr-approx/LEATHER-white.material"',
    runtime_mix_text,
)
self.assertIn(
    '"uri": "materials/runtime-pbr-approx/LogoSilver.material"',
    runtime_mix_text,
)
self.assertNotIn('"uri": "named:', runtime_mix_text)
```

- [x] **Step 2: Run red test**

Run:

```bash
python3 src/test/integration/test_lxe_pbrt_scene_convert.py
```

Expected: FAIL because converter currently emits `uri: named:<materialName>`.

- [x] **Step 3: Change converter URI resolution**

Change `material_ref_envelope` to accept a mapping:

```python
def material_ref_envelope(value: Any, material_uri_by_name: dict[str, str]) -> dict[str, Any]:
    if isinstance(value, list) and value:
        value = value[0]
    name = str(value)
    uri = material_uri_by_name.get(name)
    if uri is None:
        raise ValueError(f"PBRT mix references unknown material: {name}")
    return {"kind": "materialRef", "uri": uri}
```

Change `envelope_from_pbrt_param` signature:

```python
def envelope_from_pbrt_param(
    material: PbrtMaterial,
    name: str,
    material_uri_by_name: dict[str, str],
) -> dict[str, Any] | None:
```

Update the materialRef branch:

```python
if name in {"namedmaterial1", "namedmaterial2"}:
    return material_ref_envelope(param.value, material_uri_by_name)
```

Change `material_v2_bsdf_doc` signature:

```python
def material_v2_bsdf_doc(
    material: PbrtMaterial,
    defaults: dict[str, dict[str, Any]],
    material_uri_by_name: dict[str, str],
) -> tuple[dict[str, Any], dict[str, str]]:
```

When iterating explicit parameters, call:

```python
explicit = envelope_from_pbrt_param(material, name, material_uri_by_name)
```

Change `approximate_material` signature:

```python
def approximate_material(
    material: PbrtMaterial,
    defaults: dict[str, dict[str, Any]],
    material_uri_by_name: dict[str, str],
) -> tuple[dict[str, Any], list[str], str, dict[str, str]]:
```

Pass `material_uri_by_name` into `material_v2_bsdf_doc`.

In the conversion loop, build URI map before generating docs:

```python
material_uri_by_name = {
    material.name: rel_to_repo(
        out_root / "materials" / "runtime-pbr-approx" / f"{sanitize_filename(material.name)}.material",
        repo_root,
    )
    for material in scene.materials
}
```

Then call:

```python
doc, losses, strategy, parameter_sources = approximate_material(
    material, defaults, material_uri_by_name
)
```

- [x] **Step 4: Run converter test**

Run:

```bash
python3 src/test/integration/test_lxe_pbrt_scene_convert.py
```

Expected: PASS. Runtime mix material refs now use real generated `.material` URIs.

## Task 2: Migrate Default PBR Assets To Pure V2 Envelope Truth

**Files:**
- Modify: `assets/materials/pbr.material`
- Modify: `assets/materials/pbr_gold.material`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [x] **Step 1: Rewrite `pbr.material`**

Ensure `assets/materials/pbr.material` contains only:

```yaml
schema: lxe.material.v2
bsdf:
  type: uber
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    Ks: { kind: rgb, value: [0.04, 0.04, 0.04] }
    eta: { kind: float, value: 1.5 }
    uroughness: { kind: float, value: 0.5 }
    vroughness: { kind: float, value: 0.5 }
```

Remove these fields from the file:

- root `shader:`
- root `variants:`
- root `variantRules:`
- root `defaultTechnique:`
- root `techniques:`
- root `parameters:`
- root `resources:`

- [x] **Step 2: Rewrite `pbr_gold.material`**

Replace `assets/materials/pbr_gold.material` with:

```yaml
schema: lxe.material.v2
bsdf:
  type: metal
  parameters:
    eta: { kind: spectrum, value: [1.0, 0.766, 0.336] }
    k: { kind: spectrum, value: [1.0, 0.766, 0.336] }
    uroughness: { kind: float, value: 0.25 }
    vroughness: { kind: float, value: 0.25 }
```

- [x] **Step 3: Update generic loader v2 tests**

In `test_material_v2_contract_survives_generic_loading`, assert:

```cpp
REQUIRE(mat->getBsdfType() == "uber");
REQUIRE(mat->getMaterialEnvelope(StringID("Kd")).has_value());
REQUIRE(mat->getParameterBufferCount() == 0);
REQUIRE(mat->getPassShader(Pass_Forward) == nullptr);
REQUIRE(!mat->readParameterValue(StringID("MaterialUBO"),
                                 StringID("baseColorFactor"))
             .has_value());
```

In `test_pbr_example_material_loads`, assert:

```cpp
REQUIRE(mat->getBsdfType() == "metal");
REQUIRE(mat->getMaterialEnvelope(StringID("eta")).has_value());
REQUIRE(mat->getMaterialEnvelope(StringID("k")).has_value());
REQUIRE(mat->getParameterBufferCount() == 0);
REQUIRE(mat->getPassShader(Pass_Forward) == nullptr);
REQUIRE(!mat->readParameterValue(StringID("MaterialUBO"),
                                 StringID("metallicFactor"))
             .has_value());
```

- [x] **Step 4: Run asset audit**

Run:

```bash
rg -n "MaterialUBO\\.|baseColorFactor|metallicFactor|roughnessFactor|^(parameters|resources):" assets/materials/pbr.material assets/materials/pbr_gold.material
```

Expected: no output.

## Task 3: Let GenericMaterialLoader Load Pure V2 Files Without Shader Technique Fields

**Files:**
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [x] **Step 1: Add/adjust red test for pure v2**

Ensure `test_material_v2_contract_survives_generic_loading` loads `assets/materials/pbr.material` and expects no pass shader:

```cpp
REQUIRE(mat != nullptr);
REQUIRE(mat->getBsdfType() == "uber");
REQUIRE(mat->getParameterBufferCount() == 0);
REQUIRE(mat->getPassShader(Pass_Forward) == nullptr);
```

- [x] **Step 2: Implement pure v2 fast path**

At the start of `loadGenericMaterial`, after `root` is loaded and before requiring `defaultTechnique`, add:

```cpp
if (root["schema"] && root["schema"].as<std::string>() == "lxe.material.v2") {
  LX_core::SceneResourceTable table;
  MaterialResourceParser parser;
  ParsedMaterialResource parsed = parser.parse(
      table,
      LX_core::ResourceUri(fs::relative(resolvedMaterialPath).string()),
      YAML::Dump(root));
  if (!parsed.diagnostics.empty() || !parsed.instance) {
    std::ostringstream message;
    message << resolvedMaterialPath.string() << ": invalid material v2 contract";
    for (const std::string &diagnostic : parsed.diagnostics) {
      message << "\n  " << diagnostic;
    }
    fatalLoader(message.str());
  }
  parsed.instance->syncGpuData();
  return LX_core::MaterialInstanceSharedPtr(parsed.instance.release());
}
```

Remove or bypass `applyMaterialV2ContractIfPresent()` for pure v2 files. Transitional mixed files should no longer be used by default assets after this task.

- [x] **Step 3: Run generic loader tests**

Run:

```bash
cmake --build build --target test_generic_material_loader
build/src/test/test_generic_material_loader
```

Expected: `test_material_v2_contract_survives_generic_loading` and `test_pbr_example_material_loads` pass without shader source because pure v2 no longer compiles technique shaders.

## Task 4: Remove Old PBR GPU Record Fallback

**Files:**
- Modify: `src/core/scene/scene_gpu_records.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`

- [x] **Step 1: Add upload tests for envelope-only PBR and metal**

Add a parser-driven metal upload test:

```cpp
void testMaterialV2MetalEnvelopeFeedsGpuMaterialRecord() {
  SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  auto parsed = parser.parse(table, "memory://gold.material", R"(
schema: lxe.material.v2
bsdf:
  type: metal
  parameters:
    eta: { kind: spectrum, value: [1.0, 0.766, 0.336] }
    k: { kind: spectrum, value: [1.0, 0.766, 0.336] }
    uroughness: { kind: float, value: 0.25 }
    vroughness: { kind: float, value: 0.25 }
)");
  EXPECT(parsed.instance != nullptr, "metal parser should produce instance");
  EXPECT(parsed.diagnostics.empty(), "metal parser diagnostics should be empty");
  if (!parsed.instance) {
    return;
  }
  const auto material = table.registerMaterial(std::move(parsed.instance));
  const auto mesh = table.registerMesh(uniqueMesh(makeMeshBuffer()));
  ObjectResource object;
  object.mesh = mesh;
  object.material = material;
  object.worldBounds = BoundingBox{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
  (void)table.registerObject(object);

  const auto view = table.buildUploadView();
  EXPECT(view.materials.size() == 1, "metal upload should emit record");
  if (view.materials.empty()) {
    return;
  }
  EXPECT(view.materials.front().pbrParams.x == 1.0f,
         "metal envelope should mark current GPU approximation metallic");
  EXPECT(view.materials.front().pbrParams.y == 0.25f,
         "metal uroughness should feed current GPU roughness approximation");
}
```

Extend the existing matte/v2 upload test with legacy writes after registration:

```cpp
auto resolved = table.resolve(material);
EXPECT(resolved.has_value(), "registered v2 material should resolve");
if (resolved.has_value()) {
  resolved->get().setParameter(StringID("MaterialUBO"),
                               StringID("baseColorFactor"),
                               Vec4f{0.9f, 0.1f, 0.1f, 1.0f});
  resolved->get().setParameter(StringID("MaterialUBO"),
                               StringID("metallicFactor"), 1.0f);
}
const auto afterLegacyWrite = table.buildUploadView();
EXPECT(afterLegacyWrite.materials.front().baseColor.x == 0.2f,
       "legacy baseColorFactor write must not affect v2 upload");
EXPECT(afterLegacyWrite.materials.front().pbrParams.x == 0.0f,
       "legacy metallicFactor write must not affect v2 upload");
```

- [x] **Step 2: Remove legacy PBR fallback reads**

In `toGpuMaterialRecord`, keep envelope-first behavior. In the non-envelope fallback, remove old PBR aliases:

- remove `baseColorFactor`
- remove `metallicFactor`
- remove `metallic`
- remove `roughnessFactor`
- remove `roughness`
- remove `ao`

Keep only custom/legacy non-PBR fields used by current non-v2 demos:

```cpp
constexpr std::array kLegacyCustomBindings{"MaterialUBO", "SurfaceParams"};
if (const auto value = readFirstMaterialParameter(
        material, kLegacyCustomBindings,
        std::array{"baseColor", "surfaceColor"})) {
  record.baseColor = materialValueAsColor(*value, record.baseColor);
}
if (const auto value = readFirstMaterialParameter(
        material, kLegacyCustomBindings, std::array{"specularIntensity"})) {
  record.pbrParams.z = materialValueAsFloat(*value, record.pbrParams.z);
}
if (const auto value = readFirstMaterialParameter(
        material, kLegacyCustomBindings, std::array{"ambientIntensity"})) {
  record.pbrParams.w = materialValueAsFloat(*value, record.pbrParams.w);
}
```

- [x] **Step 3: Expand envelope mapping**

In `applyMaterialV2EnvelopeRecord`, handle at least:

```cpp
if (bsdfType == "metal") {
  const auto eta = material.getMaterialEnvelope(StringID("eta"));
  if (eta.has_value()) {
    if (const auto color =
            materialEnvelopeAsColor(eta->get(), record.baseColor.w)) {
      record.baseColor = *color;
    }
  }
  record.pbrParams.x = 1.0f;
  const auto roughness = material.getMaterialEnvelope(StringID("uroughness"));
  if (roughness.has_value() && roughness->get().floatValue.has_value()) {
    record.pbrParams.y = *roughness->get().floatValue;
  }
  record.pbrParams.w = 1.0f;
  return true;
}
```

Keep matte and uber mapping from envelopes only.

- [x] **Step 4: Run scene resource table test**

Run:

```bash
cmake --build build --target test_scene_resource_table
build/src/test/test_scene_resource_table
```

Expected: parser-driven upload tests pass. If a separate test still fails on missing shader source, record it separately.

## Task 5: Tighten GenericMaterialLoader Legacy Root Field Path

**Files:**
- Modify: `src/infra/material_loader/generic_material_loader.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [x] **Step 1: Add rejection tests**

Add a test that a PBR shader with root legacy parameters is rejected:

```cpp
void test_legacy_pbr_root_parameters_are_rejected() {
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }
  auto matPath = makeTempMaterialPath("legacy_pbr_root_params");
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "shader: techniques/Forward/pbr\n"
           "defaultTechnique: Forward\n"
           "techniques:\n"
           "  Forward:\n"
           "    passes:\n"
           "      Forward:\n"
           "        shader: techniques/Forward/pbr\n"
           "        stage: raster\n"
           "parameters:\n"
           "  MaterialUBO.baseColorFactor: [1.0, 1.0, 1.0, 1.0]\n";
  }

  bool rejected = false;
  try {
    ScopedCurrentPath currentPath(root);
    (void)loadGenericMaterial(matPath);
  } catch (const std::logic_error &error) {
    rejected =
        std::string(error.what()).find("legacy PBR root parameters") !=
        std::string::npos;
  }
  REQUIRE(rejected);
}
```

Add a second test for root `resources:` on a v2 file:

```cpp
void test_material_v2_root_resources_are_rejected() {
  auto root = findProjectRoot();
  if (root.empty()) {
    std::cerr << "  SETUP: project root not found; skipping\n";
    return;
  }
  auto matPath = makeTempMaterialPath("v2_root_resources");
  ScopedTempFile tempFile(matPath);
  {
    std::ofstream out(matPath);
    out << "schema: lxe.material.v2\n"
           "bsdf:\n"
           "  type: matte\n"
           "  parameters:\n"
           "    Kd: { kind: rgb, value: [0.8, 0.8, 0.8] }\n"
           "    sigma: { kind: float, value: 0.0 }\n"
           "resources:\n"
           "  albedoMap: white\n";
  }

  bool rejected = false;
  try {
    ScopedCurrentPath currentPath(root);
    (void)loadGenericMaterial(matPath);
  } catch (const std::logic_error &error) {
    rejected =
        std::string(error.what()).find("root 'resources'") !=
        std::string::npos;
  }
  REQUIRE(rejected);
}
```

- [x] **Step 2: Implement rejection**

In `loadGenericMaterial`, before applying root params/resources:

```cpp
const auto hasLegacyPbrRootParameter = [](const YAML::Node &paramsNode) {
  if (!paramsNode || !paramsNode.IsMap()) {
    return false;
  }
  for (auto it = paramsNode.begin(); it != paramsNode.end(); ++it) {
    const std::string key = it->first.as<std::string>();
    if (key.find("baseColorFactor") != std::string::npos ||
        key.find("metallicFactor") != std::string::npos ||
        key.find("roughnessFactor") != std::string::npos ||
        key == "MaterialUBO.ao") {
      return true;
    }
  }
  return false;
};
```

For non-v2 PBR shader materials:

```cpp
if (hasLegacyPbrRootParameter(globalParamsNode)) {
  for (const auto &cp : compiledPasses) {
    if (cp.shaderName == "techniques/Forward/pbr" ||
        cp.shaderName == "techniques/Deferred/pbr_gbuffer" ||
        cp.shaderName == "techniques/Forward/pbr_clearcoat") {
      fatalLoader(resolvedMaterialPath.string() +
                  ": legacy PBR root parameters are removed; use "
                  "schema: lxe.material.v2 PBRT envelopes");
    }
  }
}
```

For pure v2 files, the fast path in Task 3 should pass the full YAML to `MaterialResourceParser`, which already rejects root `resources:` and root `parameters:`.

- [x] **Step 3: Run generic loader test**

Run:

```bash
cmake --build build --target test_generic_material_loader
build/src/test/test_generic_material_loader
```

Expected: default v2 tests and rejection tests pass.

## Task 6: Fix Current Red Tests

**Files:**
- Modify: `src/test/integration/test_material_instance.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [x] **Step 1: `test_material_instance`**

Ensure `test_material_v2_envelope_storage_disables_parameter_buffers` uses fake shader/template data and does not skip due to missing `blinnphong_0` shader source:

```cpp
auto mat = MaterialInstance::create(
    buildMultiPassTemplate(RenderState{}, RenderState{}));
```

Expected assertions:

```cpp
REQUIRE(mat->getParameterBufferCount() >= 1);
mat->setBsdfType("matte");
REQUIRE(mat->getParameterBufferCount() == 0);
REQUIRE(!mat->getParameterResource(StringID("MaterialUBO")).isValid());
```

- [x] **Step 2: `test_scene_resource_table`**

Convert any v2 assertions that write `MaterialUBO.baseColorFactor`,
`MaterialUBO.metallicFactor`, `MaterialUBO.roughnessFactor`, or
`MaterialUBO.ao` into negative checks. V2 upload expectations must come from
envelopes:

```cpp
EXPECT(upload.materials[0].baseColor.x == 1.0f,
       "v2 upload should use Kd envelope");
EXPECT(upload.materials[0].pbrParams.x == 0.0f,
       "v2 upload should not read legacy metallicFactor");
```

- [x] **Step 3: `test_generic_material_loader`**

Remove expectations that `assets/materials/pbr.material` or
`assets/materials/pbr_gold.material` expose `MaterialUBO`.

Expected replacement:

```cpp
REQUIRE(mat->getMaterialEnvelopeCount() > 0);
REQUIRE(mat->getParameterBufferCount() == 0);
REQUIRE(mat->getPassShader(Pass_Forward) == nullptr);
```

- [x] **Step 4: Run red-test set**

Run:

```bash
cmake --build build --target test_material_instance test_scene_resource_table test_generic_material_loader
ctest --test-dir build --output-on-failure -R 'test_(material_instance|scene_resource_table|generic_material_loader)'
```

Expected: these tests pass, except any remaining failure that is exclusively the known missing shader source layout for non-v2 legacy/custom materials.

## Task 7: Final 071-a Closure Verification

**Files:**
- Modify: `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`

- [x] **Step 1: Audit default/runtime PBR assets**

Run:

```bash
rg -n "MaterialUBO\\.|baseColorFactor|metallicFactor|roughnessFactor|^(parameters|resources):" assets/materials/pbr.material assets/materials/pbr_gold.material
```

Expected: no output.

- [x] **Step 2: Audit converter output contract**

Run:

```bash
python3 src/test/integration/test_lxe_pbrt_scene_convert.py
```

Expected: PASS, including no `named:` materialRef URI in runtime material v2 output.

- [x] **Step 3: Build and targeted tests**

Run:

```bash
ninja -C build test_gltf_scene_asset_loader test_scene_resource_table test_generic_material_loader test_scene_runtime test_material_instance test_material_v2_parser test_material_v2_resource_dependencies
ctest --test-dir build --output-on-failure -R 'test_(material_instance|material_v2_parser|material_v2_resource_dependencies|scene_resource_table|generic_material_loader|scene_runtime|gltf_scene_asset_loader)'
```

Expected: PASS. If tests outside the pure v2 path still fail because current worktree lacks shader source files under `assets/shaders/glsl/techniques/`, record exact failing test and path.

- [x] **Step 4: Whitespace check**

Run:

```bash
git diff --check -- assets/materials src/tools/lxe_pbrt_scene_convert src/core/scene src/infra/material_loader src/test/integration notes/requirements/071-a-material-v2-pbrt-surface-contract.md
```

Expected: no output.

- [x] **Step 5: Update requirement status**

Append to `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`:

```markdown
### 2026-06-11: 071-a default material truth closed

- Default PBR runtime assets use pure `lxe.material.v2` PBRT envelopes with no root `parameters:` / `resources:` and no `MaterialUBO.*` truth.
- PBRT converter mix material references now emit real generated `.material` URIs instead of `named:` placeholders.
- GPU material upload reads migrated PBRT envelopes and no longer falls back to old PBR names (`baseColorFactor`, `metallicFactor`, `roughnessFactor`, `ao`).
- `GenericMaterialLoader` rejects removed legacy PBR root parameter contracts.
- Remaining root-parameter materials are custom legacy/demo shader materials, not the default PBRT/PBR runtime path; they are deferred to the MaterialTemplate/Technique boundary cleanup.
- Verification results: record the exact targeted Ninja build, targeted CTest, converter test, and asset audit outputs from this implementation pass.
```

## Self-Review

- R2: strengthened because runtime assets and converter output no longer use root legacy parameter truth.
- R2.1/T3: preserved because materialRef still passes through `MaterialResourceParser` with real URI dependency handles.
- R3/T2: converter default reporting remains unchanged; materialRef URI bug is fixed without adding runtime source fields.
- R5: reinforced because v2 `MaterialInstance` keeps envelope truth and upload reads envelopes.
- R7: primary target of this plan; old default PBR root parameters and GPU fallback are removed.
- R8: partially addressed for default PBRT path through bindless upload records; full technique reflection validation belongs to REQ-071-b.
- T6/T7: pure v2 material loading no longer depends on shader technique fields, reducing current shader-layout blocker for material parser/generic loader tests.
