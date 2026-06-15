# Shader Layout, Common GLSL, and Forward Uber Shader Design

## Goal

Reorganize `assets/shaders/glsl` so shader sources are grouped by pass and use shared GLSL modules instead of duplicating ABI declarations, math helpers, lighting code, ray-scene records, and traversal logic. The change also prepares a bindless-ready forward Uber shader, but does not enable bindless Vulkan descriptors or route runtime rendering through the Uber path yet.

This design intentionally covers shader source layout, GLSL common modules, build/test path migration, traditional forward ABI convergence, and a compile-validated Uber shader. It does not implement descriptor indexing, a global bindless descriptor set, GPU-driven drawing, or runtime material migration to SSBO-backed Uber rendering.

## Directory Layout

`assets/shaders/glsl` will stop storing concrete shader files at the root. The root will contain only category directories:

```text
assets/shaders/glsl/
  common/
    abi/
    geometry/
    lighting/
    material/
    post/
    ray/
    scene/
  forward/
    blinnphong_0.vert
    blinnphong_0.frag
    pbr.vert
    pbr.frag
    uber_forward.vert
    uber_forward.frag
  shadow/
    shadow_depth_only.vert
    shadow_depth_only.frag
  post/
    post_process.vert
    post_process.frag
    bloom_threshold.vert
    bloom_threshold.frag
    bloom_blur_h.vert
    bloom_blur_h.frag
    bloom_blur_v.vert
    bloom_blur_v.frag
  ibl/
    equirect_to_cubemap.vert
    equirect_to_cubemap.frag
    ibl_irradiance_convolve.vert
    ibl_irradiance_convolve.frag
    ibl_prefilter_env.vert
    ibl_prefilter_env.frag
    ibl_brdf_lut.vert
    ibl_brdf_lut.frag
    skybox.vert
    skybox.frag
    texture_cube_probe.vert
    texture_cube_probe.frag
  debug/
    debug_line.vert
    debug_line.frag
    mesh_debug.vert
    mesh_debug.frag
    minimal.vert
    minimal.frag
  experimental/
    rtr_experiment_template.vert
    rtr_experiment_template.frag
    rtr_shadertoy_quantum_core.vert
    rtr_shadertoy_quantum_core.frag
  offline/
    offline_pbr_direct_ray.comp
```

The migration is one-way. Existing C++ code, material assets, tests, shader README content, and build scripts will be updated to use the new paths. Root-level forwarding shader files or compatibility copies will not be kept.

The shader CMake target will recursively collect `*.vert`, `*.frag`, and `*.comp`. SPIR-V output will preserve the source-relative directory path, such as `build/assets/shaders/glsl/forward/pbr.frag.spv`, to avoid future name collisions across directories.

## Traditional Forward ABI

The existing forward shaders will converge on one stable system ABI instead of using preprocessor macros to paper over incompatible set numbers.

The traditional raster ABI is:

```text
set 0: frame and scene resources
  binding 0: CameraUBO
  binding 1: LightUBO / SceneLightUBO
  binding 2: ShadowMap0
  binding 3: ShadowMap1
  binding 4: ShadowMap2
  binding 5: ShadowMap3

set 1: material-owned traditional resources
  binding 0: MaterialUBO
  binding 1..N: material textures

set 2: environment and IBL resources
  binding 0: IrradianceMap
  binding 1: PrefilteredEnvMap
  binding 2: BrdfLut
  binding 3: EnvironmentUBO

set 3: object and animation resources
  binding 0: Bones
```

`pbr` and `blinnphong_0` remain separate traditional shader families because they are different shading models. Their system resources, material resource ownership, and descriptor set positions must be compatible. The implementation must update backend descriptor injection, material loader assumptions, reflection tests, and path tests where the current Blinn-Phong layout differs from this ABI.

## Common GLSL Modules

Common files use `lx` prefixes for public structs, functions, constants, and helpers. Files that declare shader-visible resources stay separate from pure helper files so that including a math helper cannot accidentally change a shader's reflected descriptor contract.

Planned modules:

```text
common/abi/
  frame_ubo.glsl
  light_ubo.glsl
  material_legacy.glsl
  skinning_ubo.glsl
  environment_ubo.glsl
  bindless_scene.glsl
  bindless_material.glsl

common/geometry/
  fullscreen_triangle.glsl
  cube_capture.glsl
  transform.glsl
  normals.glsl

common/lighting/
  pbr_brdf.glsl
  blinn_phong.glsl
  shadow_cascade.glsl

common/material/
  pbr_eval.glsl
  blinn_eval.glsl
  uber_material.glsl

common/post/
  tone_mapping.glsl
  bloom.glsl

common/ray/
  random.glsl
  intersection.glsl
  bvh_traversal.glsl
  camera_ray.glsl

common/scene/
  records.glsl
  buffers.glsl
```

Examples of expected extraction:

- Fullscreen triangle position and UV helpers move out of post, bloom, and BRDF LUT shaders.
- Cube capture vertex arrays move out of equirectangular conversion and IBL convolution/prefilter shaders.
- PBR BRDF functions move out of `pbr.frag`.
- TBN construction, flat-normal fallback, and normal-map decode move into geometry/material helpers.
- Bloom horizontal and vertical shaders share a single blur helper that takes axis or texel direction.
- Cascade shadow selection, debug color, and PCF logic move out of `blinnphong_0.frag`.
- Offline ray records, BVH node decoding, AABB intersection, triangle intersection, scene traversal, random hash, and camera ray generation move out of `offline_pbr_direct_ray.comp`.

`common/scene/records.glsl` and `common/scene/buffers.glsl` are the shared GPU scene table ABI used by both the forward Uber shader and offline/RTRT-style compute shaders. Offline rendering is a ray-trace consumer of this ABI; the Uber shader is a raster forward consumer.

## Shared GPU Scene Table

The shared SSBO model must be designed before the Uber shader and offline shader diverge. The core records will include:

```text
lxVertexRecord
lxMeshRecord
lxPrimitiveRecord
lxObjectRecord
lxMaterialRecord
lxBvhNode
```

The corresponding buffers will expose:

```text
lxVertices[]
lxIndices[]
lxMeshes[]
lxPrimitives[]
lxObjects[]
lxMaterials[]
lxBvhNodes[]
```

The `lxMaterialRecord` is PBR-first because the forward Uber shader must support PBR from the beginning, and offline shading should move toward the same material model. It should carry at least base color, metallic, roughness, ambient occlusion, emissive, shading model, alpha mode, material flags, and texture indices for albedo, normal, metallic-roughness, AO, and emissive resources.

Offline/RTRT usage:

- Trace BVH or primitive data to recover `materialIndex`.
- Fetch `lxMaterialRecord`.
- Evaluate PBR or fallback shading using common material/light helpers.

Forward Uber raster usage:

- Use draw/object data to recover `objectIndex` and `materialIndex`.
- Fetch the same `lxObjectRecord` and `lxMaterialRecord`.
- Evaluate shading from the same material fields and texture index semantics.

If current C++ offline record layouts cannot match the new shader-side records exactly, the implementation must update the C++ record structs or explicitly document a narrow transitional mismatch in the implementation plan. The design target is one shared shader ABI, not two similar-but-incompatible record sets.

## Forward Uber Shader

`forward/uber_forward.vert` and `forward/uber_forward.frag` are source-level preparation for bindless rendering. They must compile and reflect correctly, but they are not connected to the current runtime forward renderer.

The Uber shader will:

- Use SSBO-backed scene/object/material records instead of traditional `MaterialUBO` plus per-material sampler bindings.
- Share `common/scene/records.glsl`, `common/scene/buffers.glsl`, and PBR material helpers with offline/RTRT shaders.
- Support PBR in the initial version.
- Allow Blinn-Phong or other shading models through material record fields and shared evaluation helpers.
- Use texture indices stored in `lxMaterialRecord`.
- Use a helper such as `lxSampleMaterialTexture2D(index, uv, fallback)` that applies `nonuniformEXT(index)` for bindless texture array access.
- Prefer uniform branches from `lxMaterialRecord.flags` for ordinary material features such as normal map, metallic-roughness map, AO map, and emissive map.
- Use specialization constants only for a small number of pipeline-shaping dimensions, such as shading model family policy, alpha mode policy, or normal path policy.
- Avoid conditionally compiled material feature permutations. Preprocessor conditionals should be limited to backend/platform capability gates such as `LX_ENABLE_BINDLESS_TEXTURES`.

This keeps PSO count lower than traditional permutation-heavy shader families while still allowing a few pipeline-critical constants to be specialized when needed.

## Offline and RTRT Reuse

`offline/offline_pbr_direct_ray.comp` will be refactored to consume common scene, ray, material, and lighting modules. Its special-purpose compute entry point remains local to `offline/`, but the following logic becomes reusable:

- `lxWangHash` and `lxRandom01`
- `lxTransformPoint`
- `lxIntersectAabb`
- `lxIntersectTriangle`
- BVH leaf/internal node decoding
- `lxTraceScene`
- camera ray construction from camera basis vectors
- material record decode
- PBR shading helpers where applicable

Future RTRT shaders should be able to include the same scene/ray modules and differ only in entry points, ray generation policy, output resources, and higher-level shading choices.

## Build and Test Plan

The shader build target will recursively compile all stage files and track includes through depfiles. Common GLSL files are not compiled directly; they are validated through the shaders that include them.

Tests will update existing hard-coded paths and add coverage for:

- Traditional `forward/pbr.vert|frag` compile and reflect under the converged ABI.
- Traditional `forward/blinnphong_0.vert|frag` compile and reflect under the converged ABI.
- `offline/offline_pbr_direct_ray.comp` compiles after common ray/module extraction.
- `forward/uber_forward.vert|frag` compiles and reflects its SSBO scene/material resources and bindless texture declarations.
- IBL, post, debug, shadow, experimental, and minimal shaders compile from their new directories.
- Asset layout tests expect the new directory structure and no root-level concrete shader files.

Runtime verification after implementation should include at least `ninja CompileShaders`, `ninja test_shader_compiler`, and the existing material/asset layout tests affected by shader path and reflection changes.

## Risks and Constraints

- ABI convergence is not a cosmetic shader edit. It requires descriptor injection, material loader, and tests to agree with the new set/binding positions.
- The Uber shader must not be presented as runtime bindless support. Descriptor indexing and global bindless set management remain future work.
- Shared SSBO records must stay synchronized between shader and C++ producers. The implementation should prefer one clearly named record contract over ad hoc per-shader structs.
- Specialization constants should be used sparingly. The default material feature path is record-driven uniform branching, not conditionally compiled permutations.
- Moving shader outputs into subdirectories changes runtime asset paths. All references must be updated in one pass because no root compatibility files will remain.
