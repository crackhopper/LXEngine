# Damaged Helmet Direct Reference Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the default Damaged Helmet scene a direct-light-only reference scene and fix the standard-PBR direct BRDF gap so the helmet's metallic/roughness texture response is visibly closer to the Khronos reference screenshot.

**Architecture:** Keep the default generated scene as the comparison target: one helmet, one directional light, no ground, no environment, no skybox. Validate the asset/import path separately from shader behavior, then fix standard-PBR direct lighting at the material contract/common PBR boundary so Forward and OfflineRT continue sharing `lxEvaluateBsdf()`.

**Tech Stack:** C++20 integration tests, Python converter/smoke tests, GLSL shader contracts, Vulkan realtime smoke through `lxe_realtime_render.py`, lxe_editor API dump under Xvfb, MCP remote verification.

---

## File Structure

- Create `assets/reference/damaged_helmet/khronos_screenshot.png`: committed Khronos reference screenshot.
- Create `assets/reference/damaged_helmet/README.md`: attribution and license note for the screenshot.
- Modify `src/test/integration/test_lxe_gltf_material_convert.py`: assert the default generated scene remains direct-only and uses the chosen reference camera/light setup.
- Modify `src/test/integration/test_gltf_scene_asset_loader.cpp`: assert Damaged Helmet is one mesh/one primitive/one material and its metallic-roughness texture carries nontrivial channel data through existing `TextureLoader`.
- Modify `src/test/integration/test_shader_compiler.cpp`: add a shader audit proving standard-PBR no longer delegates direct evaluation to Lambert and uses the shared GGX helper.
- Modify `src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py`: update the converter's generated scene camera, output directory, and directional light to match the direct reference scene.
- Modify `assets/scenes/generated/helmet_standard_pbr.scene.yaml`: keep it in sync with converter output.
- Modify `assets/shaders/glsl/common/pbr.glsl`: add a direct BRDF helper that does not multiply by light radiance or `NdotL`.
- Modify `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl`: evaluate standard-PBR direct BRDF through the shared GGX helper.
- Generate non-committed visual outputs under `artifacts/reference/damaged_helmet_direct/` for local inspection and final reporting.

---

### Task 1: Add Khronos Reference Screenshot

**Files:**
- Create: `assets/reference/damaged_helmet/khronos_screenshot.png`
- Create: `assets/reference/damaged_helmet/README.md`

- [ ] **Step 1: Download the reference image**

Run:

```bash
mkdir -p assets/reference/damaged_helmet
curl -L -o assets/reference/damaged_helmet/khronos_screenshot.png \
  https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/screenshot/screenshot.png
file assets/reference/damaged_helmet/khronos_screenshot.png
```

Expected:

```text
assets/reference/damaged_helmet/khronos_screenshot.png: PNG image data
```

- [ ] **Step 2: Add attribution and license note**

Create `assets/reference/damaged_helmet/README.md` with:

```markdown
# Damaged Helmet Reference Screenshot

This directory contains the Khronos Damaged Helmet reference screenshot used for
LXEngine rendering diagnostics.

Source page:
https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/DamagedHelmet/README.md

Screenshot source:
https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/screenshot/screenshot.png

The Khronos README credits the glTF rebuild/conversion under CC BY 4.0 and the
earlier model version under CC BY-NC 4.0. This screenshot is committed only as a
diagnostic reference image for visual comparison. It is not a CC0 runtime asset.
```

- [ ] **Step 3: Verify only reference files are staged**

Run:

```bash
git status --short assets/reference/damaged_helmet
```

Expected:

```text
?? assets/reference/damaged_helmet/
```

- [ ] **Step 4: Commit**

Run:

```bash
git add assets/reference/damaged_helmet/khronos_screenshot.png \
  assets/reference/damaged_helmet/README.md
git commit -m "test: add damaged helmet reference screenshot"
```

---

### Task 2: Add Asset And Scene Diagnostic Tests

**Files:**
- Modify: `src/test/integration/test_lxe_gltf_material_convert.py`
- Modify: `src/test/integration/test_gltf_scene_asset_loader.cpp`

- [ ] **Step 1: Add Python glTF shape and direct scene assertions**

In `src/test/integration/test_lxe_gltf_material_convert.py`, add `import json` near the existing imports:

```python
import argparse
import json
import shutil
```

Add this test method to `GltfMaterialConvertTest`:

```python
    def test_damaged_helmet_source_is_single_standard_pbr_material(self) -> None:
        gltf_path = (
            self.source_dir
            / "assets"
            / "models"
            / "damaged_helmet"
            / "DamagedHelmet.gltf"
        )
        doc = json.loads(gltf_path.read_text(encoding="utf-8"))

        self.assertEqual(len(doc["materials"]), 1)
        self.assertEqual(len(doc["meshes"]), 1)
        primitives = doc["meshes"][0]["primitives"]
        self.assertEqual(len(primitives), 1)
        self.assertEqual(primitives[0]["material"], 0)
        self.assertEqual(
            sorted(primitives[0]["attributes"].keys()),
            ["NORMAL", "POSITION", "TEXCOORD_0"],
        )

        material = doc["materials"][0]
        self.assertEqual(material["name"], "Material_MR")
        pbr = material["pbrMetallicRoughness"]
        self.assertIn("baseColorTexture", pbr)
        self.assertIn("metallicRoughnessTexture", pbr)
        self.assertIn("normalTexture", material)
        self.assertIn("occlusionTexture", material)
        self.assertIn("emissiveTexture", material)

        images = [image["uri"] for image in doc["images"]]
        self.assertEqual(
            images,
            [
                "Default_albedo.jpg",
                "Default_metalRoughness.jpg",
                "Default_emissive.jpg",
                "Default_AO.jpg",
                "Default_normal.jpg",
            ],
        )
```

In `test_damaged_helmet_conversion_writes_standard_pbr_scene`, after `self.assertIn("name: Helmet Standard PBR", scene_text)`, add:

```python
            self.assertIn("enabled: false", scene_text)
            self.assertIn("skyboxEnabled: false", scene_text)
            self.assertIn("shadows: false", scene_text)
            self.assertIn("outDir: artifacts/reference/damaged_helmet_direct", scene_text)
            self.assertIn("translation: [-1.2, 0.0, 3.2]", scene_text)
            self.assertIn("rotation: [0.983954, 0.0, -0.178425, 0.0]", scene_text)
            self.assertIn("fovY: 35.0", scene_text)
            self.assertIn("kind: Directional", scene_text)
            self.assertIn("direction: [-0.45, -0.75, -0.48]", scene_text)
            self.assertIn("intensity: 4.0", scene_text)
            self.assertNotIn("ground", scene_text.lower())
```

- [ ] **Step 2: Add C++ metallic-roughness texture variation test**

In `src/test/integration/test_gltf_scene_asset_loader.cpp`, add the include:

```cpp
#include "infra/texture_loader/texture_loader.hpp"
```

Add this helper and test in the anonymous namespace after `expectTextureEnvelope`:

```cpp
struct ChannelStats {
  std::uint64_t sum = 0;
  int min = 255;
  int max = 0;
  int lowCount = 0;
  int highCount = 0;
};

ChannelStats channelStats(const unsigned char *rgba, int pixelCount,
                          int channel) {
  ChannelStats stats;
  for (int index = 0; index < pixelCount; ++index) {
    const int value = rgba[index * 4 + channel];
    stats.sum += std::uint64_t(value);
    stats.min = std::min(stats.min, value);
    stats.max = std::max(stats.max, value);
    if (value < 64) {
      ++stats.lowCount;
    }
    if (value >= 180) {
      ++stats.highCount;
    }
  }
  return stats;
}

void testDamagedHelmetMetallicRoughnessTextureHasVariation() {
  const bool found =
      cdToWhereAssetsExist("models/damaged_helmet/Default_metalRoughness.jpg");
  expect(found, "DamagedHelmet metallic-roughness texture must exist");

  infra::TextureLoader loader;
  loader.load("assets/models/damaged_helmet/Default_metalRoughness.jpg");
  expect(loader.getWidth() == 2048, "metallic-roughness texture width");
  expect(loader.getHeight() == 2048, "metallic-roughness texture height");
  const int pixelCount = loader.getWidth() * loader.getHeight();
  const unsigned char *rgba = loader.getData();
  expect(rgba != nullptr, "metallic-roughness texture pixels");

  const ChannelStats roughness = channelStats(rgba, pixelCount, 1);
  const ChannelStats metallic = channelStats(rgba, pixelCount, 2);
  expect(roughness.min < 32, "roughness green channel should include low values");
  expect(roughness.max > 220,
         "roughness green channel should include high values");
  expect(roughness.lowCount > pixelCount / 5,
         "roughness green channel should vary across material regions");
  expect(roughness.highCount > pixelCount / 10,
         "roughness green channel should include rough material regions");
  expect(metallic.max > 220,
         "metallic blue channel should include metallic regions");
  expect(metallic.highCount > pixelCount / 4,
         "metallic blue channel should not be mostly zero");
}
```

In `main()`, call the new test after `testDamagedHelmetLoadsStandardPbrCleanPath();`:

```cpp
  testDamagedHelmetMetallicRoughnessTextureHasVariation();
```

- [ ] **Step 3: Run diagnostic tests and verify expected failure**

Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_lxe_gltf_material_convert|test_gltf_scene_asset_loader'
```

Expected before implementation:

```text
test_lxe_gltf_material_convert ... Failed
```

`test_gltf_scene_asset_loader` may already pass because the current checked-in asset already has correct texture variation. That is acceptable for this diagnostic lock-in test.

- [ ] **Step 4: Commit failing diagnostics**

Run:

```bash
git add src/test/integration/test_lxe_gltf_material_convert.py \
  src/test/integration/test_gltf_scene_asset_loader.cpp
git commit -m "test: lock damaged helmet direct reference inputs"
```

---

### Task 3: Update The Default Helmet Scene And Converter

**Files:**
- Modify: `src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py`
- Modify: `assets/scenes/generated/helmet_standard_pbr.scene.yaml`

- [ ] **Step 1: Update converter scene template**

In `write_scene()` in `src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py`, change the preview output path and camera/light block to:

```yaml
      outDir: artifacts/reference/damaged_helmet_direct
```

```yaml
    - nodeName: game_camera
      name: game_cam
      transform:
        translation: [-1.2, 0.0, 3.2]
        rotation: [0.983954, 0.0, -0.178425, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      camera:
        type: perspective
        fovY: 35.0
        aspect: 1.0
        nearPlane: 0.1
        farPlane: 20.0
        cullingMask: 4294967295
```

```yaml
    - nodeName: compare_key_light
      name: compare_key_light
      transform:
        translation: [0.0, 4.0, 4.0]
        rotation: [1.0, 0.0, 0.0, 0.0]
        scale: [1.0, 1.0, 1.0]
      visibilityMask: 4294967295
      light:
        kind: Directional
        direction: [-0.45, -0.75, -0.48]
        color: [1.0, 1.0, 1.0]
        intensity: 4.0
        shadowStrength: 0.0
        shadowDistance: 80.0
        shadowCascadeCount: 1
```

- [ ] **Step 2: Update checked-in default scene**

Apply the same changes to `assets/scenes/generated/helmet_standard_pbr.scene.yaml`.

- [ ] **Step 3: Run converter and scene tests**

Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_lxe_gltf_material_convert|test_gltf_scene_asset_loader'
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Commit scene/converter update**

Run:

```bash
git add src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py \
  assets/scenes/generated/helmet_standard_pbr.scene.yaml
git commit -m "test: align damaged helmet direct scene"
```

---

### Task 4: Add Shader Red Test For Standard-PBR Direct GGX

**Files:**
- Modify: `src/test/integration/test_shader_compiler.cpp`

- [ ] **Step 1: Add shader audit test**

Add this function near `testSharedPbrKeepsLowRoughnessHighlights`:

```cpp
static bool testStandardPbrContractUsesSharedGgxDirectBsdf(
    const std::filesystem::path &shaderDir) {
  std::cout << "  Test: standard-pbr contract uses shared GGX direct BSDF\n";
  const auto commonPbr = readTextFile(shaderDir / "common" / "pbr.glsl");
  const auto standardPbr =
      readTextFile(shaderDir / "common" / "materials" /
                   "standard_pbr.contract.glsl");

  if (commonPbr.find("vec3 lxPbrDirectBrdf(") == std::string::npos) {
    std::cerr << "  FAIL: common/pbr.glsl should expose lxPbrDirectBrdf\n";
    return false;
  }
  if (standardPbr.find("lxEvaluateLambertLikeBsdf(bsdfInput)") !=
      std::string::npos) {
    std::cerr << "  FAIL: standard-pbr direct evaluation still delegates to "
                 "Lambert-like BSDF\n";
    return false;
  }
  if (standardPbr.find("lxPbrDirectBrdf(pbrInput)") == std::string::npos) {
    std::cerr << "  FAIL: standard-pbr direct evaluation should use shared "
                 "GGX direct BRDF\n";
    return false;
  }
  if (standardPbr.find("bsdfInput.metallic") == std::string::npos ||
      standardPbr.find("bsdfInput.roughness") == std::string::npos) {
    std::cerr << "  FAIL: standard-pbr direct evaluation should consume "
                 "metallic and roughness\n";
    return false;
  }

  std::cout << "  PASS: standard-pbr contract uses shared GGX direct BSDF\n";
  return true;
}
```

Call it in `main()` after `testSharedPbrKeepsLowRoughnessHighlights(shaderDir)`:

```cpp
  if (!testStandardPbrContractUsesSharedGgxDirectBsdf(shaderDir))
    ++failures;
```

- [ ] **Step 2: Run test and verify it fails**

Run:

```bash
cmake --build build --target test_shader_compiler
ctest --test-dir build --output-on-failure -R '^test_shader_compiler$'
```

Expected:

```text
FAIL: common/pbr.glsl should expose lxPbrDirectBrdf
```

- [ ] **Step 3: Commit failing shader audit**

Run:

```bash
git add src/test/integration/test_shader_compiler.cpp
git commit -m "test: require standard pbr direct ggx bsdf"
```

---

### Task 5: Implement Shared GGX Direct BSDF

**Files:**
- Modify: `assets/shaders/glsl/common/pbr.glsl`
- Modify: `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl`

- [ ] **Step 1: Add `lxPbrDirectBrdf`**

In `assets/shaders/glsl/common/pbr.glsl`, replace the body of `lxPbrDirectLight` by first adding this function above it:

```glsl
vec3 lxPbrDirectBrdf(LxPbrDirectInput pbr) {
  vec3 N = normalize(pbr.normal);
  vec3 V = normalize(pbr.viewDir);
  vec3 L = normalize(pbr.lightDir);
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  if (NdotV <= 0.0 || NdotL <= 0.0) {
    return vec3(0.0);
  }

  vec3 H = normalize(V + L);
  float roughness = clamp(pbr.roughness, 0.04, 1.0);
  float metallic = clamp(pbr.metallic, 0.0, 1.0);

  vec3 F0 = lxPbrF0(pbr.baseColor, metallic);
  float NDF = lxDistributionGGX(N, H, roughness);
  float G = lxGeometrySmith(N, V, L, roughness);
  vec3 F = lxFresnelSchlick(max(dot(H, V), 0.0), F0);

  vec3 numerator = NDF * G * F;
  float denominator = 4.0 * NdotV * NdotL + 0.0001;
  vec3 specular = numerator / denominator;

  vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
  return kD * pbr.baseColor / LX_PBR_PI + specular;
}
```

Then make `lxPbrDirectLight` call it:

```glsl
vec3 lxPbrDirectLight(LxPbrDirectInput pbr) {
  vec3 N = normalize(pbr.normal);
  vec3 L = normalize(pbr.lightDir);
  float NdotL = max(dot(N, L), 0.0);
  return lxPbrDirectBrdf(pbr) * pbr.lightColor * NdotL;
}
```

- [ ] **Step 2: Update standard-PBR contract**

In `assets/shaders/glsl/common/materials/standard_pbr.contract.glsl`, add the shared PBR include after the existing includes:

```glsl
#include "../material_surface.glsl"
#include "../material_bsdf.glsl"
#include "../pbr.glsl"
```

Replace `lxEvaluateBsdf` with:

```glsl
LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput) {
  LxPbrDirectInput pbrInput;
  pbrInput.baseColor = max(bsdfInput.baseColor, vec3(0.0));
  pbrInput.normal = bsdfInput.normal;
  pbrInput.viewDir = bsdfInput.wo;
  pbrInput.lightDir = bsdfInput.wi;
  pbrInput.lightColor = vec3(1.0);
  pbrInput.metallic = bsdfInput.metallic;
  pbrInput.roughness = bsdfInput.roughness;
  pbrInput.ao = bsdfInput.ao;
  pbrInput.emissive = bsdfInput.emissive;

  LxBsdfEvaluateOutput result;
  result.value = lxPbrDirectBrdf(pbrInput);
  return result;
}
```

Leave `lxSampleBsdf` unchanged in this slice.

- [ ] **Step 3: Build and run shader tests**

Run:

```bash
cmake --build build --target test_shader_compiler
ctest --test-dir build --output-on-failure -R '^test_shader_compiler$'
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Run shader audit commands**

Run:

```bash
rg -n "lxEvaluateLambertLikeBsdf\\(bsdfInput\\)" \
  assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
rg -n "vec3 lxPbrDirectBrdf|lxPbrDirectBrdf\\(pbrInput\\)" \
  assets/shaders/glsl/common/pbr.glsl \
  assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
```

Expected:

```text
first rg command prints no matches
second rg command prints lxPbrDirectBrdf definitions/usages
```

- [ ] **Step 5: Commit shader fix**

Run:

```bash
git add assets/shaders/glsl/common/pbr.glsl \
  assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
git commit -m "fix: evaluate standard pbr direct ggx"
```

---

### Task 6: Run Local Smoke, Dump, And Visual Inspection

**Files:**
- Generate: `artifacts/reference/damaged_helmet_direct/realtime_result.json`
- Generate: `artifacts/reference/damaged_helmet_direct/hdr-color.bmp`
- Generate: `artifacts/reference/damaged_helmet_direct/visual-comparison.md`

- [ ] **Step 1: Run focused local test set**

Run:

```bash
ctest --test-dir build --output-on-failure -R 'test_lxe_gltf_material_convert|test_gltf_scene_asset_loader|test_shader_compiler'
```

Expected:

```text
100% tests passed
```

- [ ] **Step 2: Run realtime smoke and save payload**

Run:

```bash
mkdir -p artifacts/reference/damaged_helmet_direct
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile preview \
  --xvfb \
  --require-nonblack \
  --require-pipeline-metadata \
  --project-name codex_helmet_direct_reference \
  | tee artifacts/reference/damaged_helmet_direct/realtime_result.json
```

Expected:

```text
JSON payload with cpuSrgbPngPath, metadataPath, and renderInputStats
```

- [ ] **Step 3: Capture `hdr.color` dump through editor API**

Run:

```bash
xvfb-run -a python3 - <<'PY'
from pathlib import Path
import shutil

from tests.lxe_editor.api_client import LxeEditorHarness

repo = Path.cwd()
out_dir = repo / "artifacts" / "reference" / "damaged_helmet_direct"
out_dir.mkdir(parents=True, exist_ok=True)

harness = LxeEditorHarness()
project_root = (
    harness.client.runtime_root
    / "data"
    / "projects"
    / "helmet_direct_reference_dump"
)
scene_dst = project_root / "scenes" / "helmet_standard_pbr.scene.yaml"
scene_dst.parent.mkdir(parents=True, exist_ok=True)
scene_dst.write_text(
    (repo / "assets" / "scenes" / "generated" / "helmet_standard_pbr.scene.yaml").read_text(
        encoding="utf-8"
    ),
    encoding="utf-8",
)
assets_dst = project_root / "assets"
if not assets_dst.exists():
    try:
        assets_dst.symlink_to(repo / "assets", target_is_directory=True)
    except OSError:
        shutil.copytree(repo / "assets", assets_dst, dirs_exist_ok=True)
(project_root / "project.yaml").write_text(
    "schema: lxe.project.v1\n"
    "id: helmet_direct_reference_dump\n"
    "displayName: Helmet Direct Reference Dump\n"
    "activeScene: scenes/helmet_standard_pbr.scene.yaml\n"
    "scenes:\n"
    "  - id: helmet_standard_pbr\n"
    "    path: scenes/helmet_standard_pbr.scene.yaml\n"
    "assetRoots:\n"
    "  - assets\n",
    encoding="utf-8",
)

try:
    harness.start()
    opened = harness.client.command(f"project open {project_root}")
    if not opened.get("ok"):
        raise RuntimeError(opened)

    def frame_ready():
        color = harness.client.decode_structured_json(
            harness.client.command("render debug stats hdr.color")
        )
        if float(color.get("nonZeroRatio", 0.0)) > 0.01:
            return color
        return None

    harness.client.wait_for(frame_ready, timeout_s=20.0)
    dump_path = out_dir / "hdr-color.bmp"
    dumped = harness.client.command(f"render debug dump hdr.color {dump_path}")
    if not dumped.get("ok"):
        raise RuntimeError(dumped)
    print(dumped)
finally:
    harness.stop()
PY
```

Expected:

```text
artifacts/reference/damaged_helmet_direct/hdr-color.bmp exists
```

- [ ] **Step 4: Open the reference screenshot and generated render**

Use the local image viewer tool on:

```text
assets/reference/damaged_helmet/khronos_screenshot.png
```

Parse the render payload for the generated PNG path:

```bash
python3 - <<'PY'
import json
from pathlib import Path
payload = json.loads(Path("artifacts/reference/damaged_helmet_direct/realtime_result.json").read_text())
print(payload["cpuSrgbPngPath"])
PY
```

Use the local image viewer tool on the printed PNG path and on:

```text
artifacts/reference/damaged_helmet_direct/hdr-color.bmp
```

- [ ] **Step 5: Write visual comparison note**

Create `artifacts/reference/damaged_helmet_direct/visual-comparison.md` after opening the images. Do not prefill it before viewing. The file must contain these sections and must use concrete observations from the opened images:

```markdown
# Damaged Helmet Direct Reference Visual Comparison

Reference: `assets/reference/damaged_helmet/khronos_screenshot.png`
Realtime PNG: `use the cpuSrgbPngPath printed from realtime_result.json`
HDR dump: `artifacts/reference/damaged_helmet_direct/hdr-color.bmp`

## Visual Check

- Orientation/framing:
- Albedo/material regions:
- Metallic highlights:
- Roughness variation:
- Normal detail:
- Dark damaged regions:

## Remaining Differences

- Camera pose/framing:
- Direct BRDF behavior:
- Missing environment/IBL:
- Tone mapping/exposure:
- Normal/tangent quality:
- Asset/parser mismatch:
```

Do not write "acceptable" unless the opened images visibly support that claim.

- [ ] **Step 6: If orientation is visibly wrong, do one camera/light correction**

If visual inspection shows the helmet is not a left/front three-quarter view, adjust only these fields in both `src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py` and `assets/scenes/generated/helmet_standard_pbr.scene.yaml`, then rerun Steps 1-5:

```yaml
translation: [1.2, 0.0, 3.2]
rotation: [0.983954, 0.0, 0.178425, 0.0]
```

If highlights are too weak after the shader fix and texture checks pass, adjust only directional light intensity to:

```yaml
intensity: 5.5
```

Record the final chosen camera and light values in `visual-comparison.md`.

- [ ] **Step 7: Commit scene/test finalization if Step 6 changed source files**

If source files changed during visual correction, run:

```bash
ctest --test-dir build --output-on-failure -R 'test_lxe_gltf_material_convert|test_helmet_standard_pbr_realtime_smoke'
git add src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py \
  assets/scenes/generated/helmet_standard_pbr.scene.yaml
git commit -m "test: tune damaged helmet reference view"
```

- [ ] **Step 8: Leave generated visual artifacts uncommitted**

Do not commit generated PNG/BMP/JSON artifacts or `visual-comparison.md`. Keep them local under `artifacts/reference/damaged_helmet_direct/` and summarize the visual findings in the final report. Confirm the generated files are unstaged:

```bash
git status --short artifacts/reference/damaged_helmet_direct
```

Expected: generated files are untracked or ignored; no generated artifact is staged.

---

### Task 7: Full Local Verification And Audit

**Files:**
- No new source files expected.

- [ ] **Step 1: Build touched targets**

Run:

```bash
cmake --build build --target test_shader_compiler test_gltf_scene_asset_loader lxe_editor
```

Expected:

```text
build succeeds with no new warnings from touched files
```

- [ ] **Step 2: Run required local tests**

Run:

```bash
ctest --test-dir build --output-on-failure -R '^test_shader_compiler$'
ctest --test-dir build --output-on-failure -R 'test_lxe_gltf_material_convert|test_gltf_scene_asset_loader|test_helmet_standard_pbr_realtime_smoke'
xvfb-run -a python3 -m unittest tests.lxe_editor.test_live_viewport
```

Expected:

```text
all selected tests pass
```

- [ ] **Step 3: Run required audits**

Run:

```bash
rg -n "lxEvaluateLambertLikeBsdf\\(bsdfInput\\)" \
  assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
rg -n "helmet_standard_pbr|khronos_screenshot" assets docs notes src tests
rg -n "environment:|skyboxEnabled|ground" \
  assets/scenes/generated/helmet_standard_pbr.scene.yaml
git status --short
```

Expected:

```text
first rg command prints no matches
second rg command prints expected scene/reference/test/doc hits
third rg command prints environment and skybox disabled lines, no ground node
git status shows only intended changes and known pre-existing dirty files
```

- [ ] **Step 4: Commit any remaining intended source changes**

Run `git status --short` and stage only files changed by this plan. Use this explicit staging command so unrelated dirty files stay out of the commit:

```bash
git add assets/reference/damaged_helmet/khronos_screenshot.png \
  assets/reference/damaged_helmet/README.md \
  src/test/integration/test_lxe_gltf_material_convert.py \
  src/test/integration/test_gltf_scene_asset_loader.cpp \
  src/test/integration/test_shader_compiler.cpp \
  src/tools/lxe_gltf_material_convert/lxe_gltf_material_convert.py \
  assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  assets/shaders/glsl/common/pbr.glsl \
  assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
git commit -m "fix: align damaged helmet direct pbr reference"
```

Skip this step if all intended source changes were already committed in earlier tasks.

---

### Task 8: Push And Remote MCP Verification

**Files:**
- Remote verification only.

- [ ] **Step 1: Push local commits**

Run:

```bash
git status --short
git log --oneline -5
git push
```

Expected:

```text
push succeeds; local and origin/main contain the direct-reference commits
```

- [ ] **Step 2: Pull and build remotely through MCP**

Use lxe_manager MCP:

```text
ops.repo_pull
ops.build_target {"target":"lxe_editor"}
ops.editor_restart
```

Expected:

```text
remote build succeeds and editor restarts on the pushed commit
```

- [ ] **Step 3: Load default helmet scene remotely**

Use lxe_manager MCP:

```text
lxe_editor_command {"command":"scene import assets/scenes/generated/helmet_standard_pbr.scene.yaml codex_helmet_direct_reference"}
lxe_editor_get_summary
lxe_editor_command {"command":"render debug live-stats"}
lxe_editor_command {"command":"render debug stats hdr.color"}
lxe_editor_command {"command":"render debug stats depth.main"}
```

Expected:

```text
sceneName is Helmet Standard PBR
hdr.color nonZeroRatio is greater than 0.01
depth.main has valid nonempty depth range
live stats show usedBindlessSceneDescriptors=true and fallbackObservedCount=0
```

- [ ] **Step 4: Capture remote dump or stats payload**

Use lxe_manager MCP:

```text
lxe_editor_command {"command":"render debug dump hdr.color artifacts/reference/damaged_helmet_direct/remote-hdr-color.bmp"}
```

Expected:

```text
command returns ok; if remote file access is unavailable, include hdr.color stats and live-stats in final report instead
```

- [ ] **Step 5: Final report**

Report:

- commits made;
- local build/test commands and results;
- audit command results;
- visual comparison findings from opened reference/render images;
- remote MCP build/load/stats results;
- whether any remaining difference is due to direct-only scope, camera/light setup, tone mapping, or an unresolved parser/shader defect.
