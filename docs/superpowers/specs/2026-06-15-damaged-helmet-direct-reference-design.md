# Damaged Helmet Direct Reference Design

Date: 2026-06-15

## Stage

This is a focused direct-lighting diagnostic slice for the Damaged Helmet
standard-PBR path. It intentionally excludes room geometry, HDRI environments,
IBL, point lights, skybox rendering, and render path schema changes.

The goal is to make a scene containing only:

- the Khronos Damaged Helmet asset already checked into the repository;
- one directional light;
- one camera aligned close to the Khronos reference screenshot;
- no ground, room, skybox, or environment;
- direct standard-PBR lighting only.

The implementation must explain and reduce the current large visual gap against
the Khronos reference screenshot before any environment-lighting work is added
back.

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

The apparent material variety in the Khronos screenshot is therefore encoded in
the texture maps inside a single PBR material. It is not evidence that the
helmet must have multiple submeshes or multiple glTF material slots.

The local metallic-roughness texture has nontrivial channel data:

- roughness is stored in the green channel;
- metallic is stored in the blue channel;
- the metallic channel is not uniformly zero;
- the roughness channel is not uniformly one.

The current direct-lighting shader path has a known gap. The
`standard_pbr.contract.glsl` `lxEvaluateBsdf()` function delegates to the shared
Lambert-like BSDF. Forward and OfflineRT direct lighting both call this material
contract function, so both paths currently ignore metal and roughness in the
direct BRDF value.

## Reference Asset

Commit the Khronos Damaged Helmet screenshot as a diagnostic reference:

- Source page:
  `https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/DamagedHelmet/README.md`
- Raw screenshot:
  `https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/screenshot/screenshot.png`
- Repository target:
  `assets/reference/damaged_helmet/khronos_screenshot.png`

Add attribution/license text beside the screenshot. The Khronos README credits
the rebuild/conversion under CC BY 4.0 and the earlier model version under
CC BY-NC 4.0. The screenshot is committed only as a diagnostic reference, not as
a CC0 runtime asset.

Do not commit another copy of the official glTF model unless a later audit
finds a real divergence. The checked-in model already matches the official
reference.

## Scene Design

Update the default generated helmet scene as the direct-light-only reference
scene:

```text
assets/scenes/generated/helmet_standard_pbr.scene.yaml
```

The scene contains:

- the existing Damaged Helmet model;
- the existing `damaged_helmet_standard_pbr.material`;
- exactly one directional light;
- no ground mesh;
- environment disabled;
- skybox disabled;
- shadows disabled for the first comparison pass;
- a camera adjusted to match the reference screenshot's front three-quarter
  framing as closely as practical.

The existing helmet smoke should continue using this default scene. If a
separate comparison-only output name is useful for artifacts, use the project
name or artifact directory, not a second scene file.

## Shader Design

Move standard-PBR direct lighting from Lambert-like evaluation to a shared GGX
direct BSDF. The fix belongs in the material contract/common shader boundary,
not only in `Forward/pbr.frag`, because OfflineRT also calls the material
contract `lxEvaluateBsdf()`.

The standard-PBR direct BSDF must:

- use `baseColor`, `metallic`, and `roughness`;
- use the existing GGX distribution, geometry, and Fresnel helpers;
- return a BRDF value without multiplying by light color or `NdotL`;
- keep existing Lambert-like contracts for non-standard materials unchanged.

Forward and OfflineRT should continue multiplying the returned BRDF by light
radiance and `NdotL` at the call site.

This slice does not need to replace Forward's single directional `LightUBO` with
`SceneLightsUBO`. That is required for point lights and later scene-light
unification, but it is not necessary to answer the current direct directional
helmet question.

## glTF And Material Import Diagnostics

Add tests that prove the parser/import path preserves the Damaged Helmet PBR
data:

- the local Damaged Helmet glTF has one mesh, one primitive, and one material;
- the material binds all five expected textures;
- the standard-PBR material source stores the metallic-roughness texture;
- the metallic channel is not uniformly zero and the roughness channel is not
  uniformly one;
- generated GPU material source records preserve the texture slots consumed by
  `standard_pbr.contract.glsl`;
- the generated material scalar defaults do not force metallic to zero or
  roughness to one after the texture is applied.

The tests should make the single-material fact explicit so future debugging
does not misattribute the appearance to missing submesh splitting.

Add shader tests/audits that prove the renderer gap is closed:

- `standard_pbr.contract.glsl` no longer delegates direct evaluation to
  `lxEvaluateLambertLikeBsdf(bsdfInput)`;
- the standard-PBR direct BSDF uses the shared GGX helpers;
- Forward and OfflineRT still call the material contract `lxEvaluateBsdf()`;
- the default helmet scene keeps environment and skybox disabled and has no
  ground mesh.

## Reference And Dump Outputs

After implementation, generate local outputs for the direct reference scene:

```text
artifacts/reference/damaged_helmet_direct/
```

Required outputs:

- the committed Khronos reference screenshot;
- a CPU sRGB PNG from the realtime smoke path;
- an HDR/debug color dump from `render debug dump hdr.color`;
- a JSON metadata file with render input stats;
- a short comparison note summarizing visible differences from the Khronos
  screenshot.

The comparison must explicitly classify remaining differences as one of:

- camera pose/framing;
- direct BRDF behavior;
- missing environment/IBL, if the Khronos screenshot clearly uses environment
  reflections outside the direct-light-only scope;
- tone mapping/exposure;
- normal/tangent quality;
- asset/parser mismatch.

The final report should be clear about which differences are expected because
this slice intentionally uses direct lighting only.

## Tests And Verification

Required local checks:

```bash
cmake --build build --target test_shader_compiler
ctest --test-dir build --output-on-failure -R '^test_shader_compiler$'
ctest --test-dir build --output-on-failure -R 'gltf|material|helmet|shader'
python3 src/tools/lxe_realtime_render/lxe_realtime_render.py \
  --scene assets/scenes/generated/helmet_standard_pbr.scene.yaml \
  --profile preview \
  --xvfb \
  --require-nonblack \
  --require-pipeline-metadata \
  --project-name codex_helmet_direct_reference
```

Use the editor API under Xvfb to capture at least one debug dump:

```bash
xvfb-run -a python3 -m unittest tests.lxe_editor.test_live_viewport
```

Required audits:

```bash
rg -n "lxEvaluateLambertLikeBsdf\\(bsdfInput\\)" assets/shaders/glsl/common/materials/standard_pbr.contract.glsl
rg -n "helmet_standard_pbr|khronos_screenshot" assets docs notes src tests
rg -n "environment:|skyboxEnabled|ground" assets/scenes/generated/helmet_standard_pbr.scene.yaml
```

Required remote verification after local commit and push:

- pull/build through MCP;
- load `helmet_standard_pbr.scene.yaml` in the remote editor;
- verify nonblack `hdr.color` and valid `depth.main`;
- verify live render stats still use bindless descriptors and no fallback;
- capture a remote debug dump or stats payload for the final report.

## Out Of Scope

- Room geometry.
- HDRI environment assets.
- IBL/reflection probes.
- Point lights.
- Skybox rendering.
- Render path schema changes.
- Full pixel-perfect matching with the Khronos screenshot.
- Replacing the Damaged Helmet source asset.
- Reworking Deferred lighting unless direct evidence shows the shared BSDF
  change broke Deferred or a touched test requires parity.
