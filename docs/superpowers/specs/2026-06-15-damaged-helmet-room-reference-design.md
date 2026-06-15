# Damaged Helmet Room Reference Design

Date: 2026-06-15

## Stage

This is a focused diagnostic and scene-upgrade slice for the Damaged Helmet
standard-PBR path. It must answer three questions with evidence:

- Is the checked-in Damaged Helmet glTF different from the Khronos reference?
- Does the glTF/material import path preserve the single material's PBR texture
  data, especially metallic-roughness?
- Which renderer gaps explain the missing metal highlights and missing room
  lighting?

This work does not replace the active IBL implementation. It creates a stable
room/reference test scene and closes only the direct-lighting and skybox render
path gaps needed by that scene.

## Current Facts

The local asset `assets/models/damaged_helmet/DamagedHelmet.gltf` matches the
Khronos `glTF-Sample-Models` Damaged Helmet asset byte-for-byte for the glTF,
binary, and five textures. Both the local and official glTF contain:

- one mesh;
- one primitive;
- one material named `Material_MR`;
- one base-color texture;
- one metallic-roughness texture;
- one emissive texture;
- one AO texture;
- one normal texture.

The visual impression of multiple materials therefore comes from maps inside a
single PBR material, not from multiple submeshes or multiple glTF material
slots.

The current `standard-pbr` material contract loads the PBR maps into
`LxMaterialSurface`, including:

- `baseColorTexture`;
- `metallicRoughnessTexture`, using blue for metallic and green for roughness;
- `normalTexture`;
- `occlusionTexture`;
- `emissiveTexture`.

The current direct-lighting problem is in shader consumption. The
`standard_pbr.contract.glsl` `lxEvaluateBsdf()` function returns the shared
Lambert-like BSDF, so Forward and OfflineRT direct lighting ignore metal and
roughness for the BRDF value. The Forward PBR pass also still reads a single
legacy `LightUBO` directional light instead of looping over `SceneLightsUBO`,
even though `SceneResourceTable` already builds directional and point light
entries there.

## External Assets

Use Poly Haven `Studio Small 03` as the room environment source:

- Source page: `https://polyhaven.com/a/studio_small_03`
- License: CC0 as shown on the Poly Haven asset page.
- Format: 1K EXR, approximately 21.9 MB.
- Repository target:
  `assets/textures/environment/studio_small_03_1k.exr`

Use the Khronos Damaged Helmet screenshot as a reference image:

- Source page:
  `https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/DamagedHelmet/README.md`
- Raw screenshot:
  `https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/screenshot/screenshot.png`
- Repository target:
  `assets/reference/damaged_helmet/khronos_screenshot.png`
- Add attribution/license text beside the image. The Khronos README credits
  the rebuild/conversion under CC BY 4.0 and the earlier model version under
  CC BY-NC 4.0. This image is committed only as a diagnostic reference, not as
  a CC0 runtime asset.

Do not commit another copy of the official glTF model unless a later audit
finds a real divergence. The checked-in model already matches the official
reference.

## Scene Design

Create a new scene instead of mutating the existing smoke scene in place:

```text
assets/scenes/generated/helmet_room_reference.scene.yaml
```

The scene contains:

- the existing Damaged Helmet model and standard-PBR material;
- a closed room mesh with inward-facing walls;
- a neutral matte room material;
- a point light placed above and forward of the helmet;
- a camera adjusted toward the official reference screenshot angle;
- the `Studio Small 03` EXR configured as the scene environment.

The room should be real geometry, not only a skybox. This keeps the scene useful
before IBL is complete and allows depth/dump inspection of the room. Use
inward-facing room triangles so the normal Forward path can keep back-face
culling enabled.

The environment configuration is used for environment lighting and optional
skybox debugging. The room scene should default to showing the room geometry;
skybox visibility is a separate render path capability, not the primary room
replacement.

## Render Path And Shader Design

### Direct Standard-PBR BSDF

Move standard-PBR direct lighting from Lambert-like evaluation to the shared
GGX path. The fix belongs in the material contract/common shader boundary, not
only in one Forward fragment shader branch, because OfflineRT also calls the
material contract `lxEvaluateBsdf()`.

The direct BSDF must:

- use `baseColor`, `metallic`, and `roughness`;
- use the existing GGX distribution, geometry, and Fresnel helpers;
- return a BRDF value without multiplying by light color or `NdotL`;
- keep existing Lambert-like contracts for non-standard materials unchanged.

Forward and OfflineRT should continue multiplying the returned BRDF by light
radiance and `NdotL` at the call site.

### Scene Lights In Forward

Forward PBR should consume `SceneLightsUBO` for direct lights:

- include `assets/shaders/glsl/scene_lights_ubo.glsl`;
- loop over directional lights;
- loop over point lights;
- compute point-light direction, range attenuation, and radiance;
- remove the Forward PBR fragment shader's single-light `LightUBO`
  dependency. Shadow-specific shader paths may keep `LightUBO` until their own
  lighting/shadow slice replaces it.

The room scene point light is the positive path proving point-light
consumption. A shader/source audit should fail if Forward PBR still ignores
`sceneLights.point`.

### Skybox Render Path

Add explicit skybox support to the render path schema and graph instead of
implicit backend branching.

`assets/render_paths/forward_main.render-path.yaml` should gain a `Skybox`
pass before the `Forward` geometry pass. The pass:

- declares an environment cubemap source;
- draws a fullscreen skybox shader that reconstructs view direction from the
  active camera;
- writes to `hdr.color`;
- does not write depth;
- lets later scene geometry overwrite sky pixels naturally;
- is disabled or skipped when the scene has no environment or skybox is off.

The schema/parser must reject unknown skybox fields and must not allow metadata
only environment records to satisfy live cubemap dependencies.

## glTF And Material Import Diagnostics

Add tests that prove the parser/import path preserves the Damaged Helmet PBR
data:

- the local Damaged Helmet glTF has one mesh, one primitive, and one material;
- the material binds all five expected textures;
- the standard-PBR material source stores the metallic-roughness texture;
- the metallic channel is not uniformly zero and the roughness channel is not
  uniformly one;
- generated GPU material source records preserve the texture slots consumed by
  `standard_pbr.contract.glsl`.

The tests should make the single-material fact explicit so future debugging
does not misattribute the appearance to missing submesh splitting.

Add shader tests/audits that prove the renderer gaps are closed:

- `standard_pbr.contract.glsl` no longer delegates direct evaluation to
  `lxEvaluateLambertLikeBsdf(bsdfInput)`;
- the standard-PBR direct BSDF uses the shared GGX helpers;
- `render_paths/Forward/pbr.frag` includes and consumes `SceneLightsUBO`;
- `render_paths/Forward/pbr.frag` loops over `sceneLights.point`;
- `forward_main.render-path.yaml` declares the `Skybox` pass and its
  environment source explicitly.

## Reference And Dump Outputs

Commit the reference screenshot and license note under:

```text
assets/reference/damaged_helmet/
```

After implementation, generate a local dump for the new room scene:

```text
artifacts/reference/damaged_helmet_room/
```

Required outputs:

- a CPU sRGB PNG from the realtime smoke path;
- an HDR/debug color dump from `render debug dump hdr.color`;
- a JSON metadata file with render input stats;
- a short comparison note summarizing visible differences from the Khronos
  screenshot.

The comparison should explicitly state whether remaining differences are caused
by:

- camera pose/framing;
- direct BRDF behavior;
- missing or incomplete IBL;
- tone mapping/exposure;
- normal/tangent quality;
- asset/parser mismatch.

## Tests And Verification

Required local checks:

```bash
cmake --build build --target test_shader_compiler
ctest --test-dir build --output-on-failure -R '^test_shader_compiler$'
ctest --test-dir build --output-on-failure -R 'gltf|material|helmet|render_path|work_compiler'
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py \
  --scene assets/scenes/generated/helmet_room_reference.scene.yaml \
  --profile preview \
  --xvfb \
  --require-nonblack \
  --require-pipeline-metadata \
  --project-name codex_helmet_room_reference
xvfb-run -a python3 -m unittest tests.lxe_editor.test_live_viewport
```

Required audits:

```bash
rg -n "lxEvaluateLambertLikeBsdf\\(bsdfInput\\)" assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
rg -n "sceneLights\\.point|SceneLightsUBO|LightUBO" assets/shaders/glsl/render_paths/Forward assets/render_paths src/backend src/core
rg -n "helmet_room_reference|khronos_screenshot|studio_small_03" assets docs notes src tests
```

Required remote verification after local commit and push:

- pull/build through MCP;
- load `helmet_room_reference.scene.yaml` in the remote editor;
- verify nonblack `hdr.color` and valid `depth.main`;
- verify live render stats still use bindless descriptors and no fallback;
- capture a remote debug dump or stats payload for the final report.

## Out Of Scope

- Finishing the full IBL/reflection-probe roadmap.
- Replacing the Damaged Helmet source asset.
- Adding a full material-variant authoring UI.
- Matching the Khronos screenshot pixel-for-pixel.
- Reworking Deferred lighting unless direct evidence shows the new shared BSDF
  change broke Deferred or a touched test requires parity.
