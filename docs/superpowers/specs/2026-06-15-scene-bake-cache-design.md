# Scene Bake Cache Design

Date: 2026-06-15

## Stage

Stage 2 of 3.

This stage persists generated bake outputs next to the scene file and loads
them on future scene opens. It builds on
`2026-06-15-reflection-filter-brdf-graph-design.md`.

## Decision

Generated scene bake data lives near the scene file and is ignored by git. Scene
loading does not silently rerun bake work. If a scene has no valid bake cache,
direct lighting still works and environment IBL resources are absent until the
user or editor calls `bakeScene(scene, options)`.

The cache handles the stage 1 asset types:

- global `ReflectionProbeBakeAsset`;
- `BrdfLutAsset` values required by live material contracts.

Stage 3 extends the same cache to local `ReflectionProbeComponent` assets.

## Generated File Layout

For a scene file:

```text
assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml
```

the default generated cache root is:

```text
assets/models/bmw-m6/.lxe-bake/pbrt_bmw_m6/
  scene-bake.json
  environment/
    global-reflection-probe.json
    radiance/
      mip0_px.exr
      mip0_nx.exr
      mip0_py.exr
      mip0_ny.exr
      mip0_pz.exr
      mip0_nz.exr
    prefiltered/
      mip0_px.exr
      mip0_nx.exr
      mip0_py.exr
      mip0_ny.exr
      mip0_pz.exr
      mip0_nz.exr
      mip1_px.exr
      mip1_nx.exr
      mip1_py.exr
      mip1_ny.exr
      mip1_pz.exr
      mip1_nz.exr
    diffuse_sh9.json
  brdf/
    standard-ggx-split-sum-v1.json
    standard-ggx-split-sum-v1.exr
```

The repo adds a gitignore rule for generated bake directories:

```text
**/.lxe-bake/
```

The scene file itself does not need to be rewritten after a bake. The loader
derives the default cache location from the scene URI. A future explicit
override may be added, but the first implementation keeps cache lookup
deterministic and scene-adjacent.

## Manifest Schemas

Scene cache root:

```json
{
  "schema": "lxe.scene-bake.v1",
  "scene": "pbrt_bmw_m6.scene.yaml",
  "generatedBy": "LXEngine",
  "assets": {
    "globalReflection": "environment/global-reflection-probe.json",
    "brdfLuts": {
      "standard-ggx-split-sum-v1": "brdf/standard-ggx-split-sum-v1.json"
    }
  }
}
```

Global reflection asset:

```json
{
  "schema": "lxe.reflection-probe-bake.v1",
  "kind": "global",
  "source": "assets/models/bmw-m6/textures/sky.exr",
  "prefilteredEnvMap": {
    "binding": "PrefilteredEnvMap",
    "format": "RGBA16Float",
    "extent": 64,
    "mips": 2,
    "faces": {
      "mip0_px": "prefiltered/mip0_px.exr",
      "mip0_nx": "prefiltered/mip0_nx.exr",
      "mip0_py": "prefiltered/mip0_py.exr",
      "mip0_ny": "prefiltered/mip0_ny.exr",
      "mip0_pz": "prefiltered/mip0_pz.exr",
      "mip0_nz": "prefiltered/mip0_nz.exr",
      "mip1_px": "prefiltered/mip1_px.exr",
      "mip1_nx": "prefiltered/mip1_nx.exr",
      "mip1_py": "prefiltered/mip1_py.exr",
      "mip1_ny": "prefiltered/mip1_ny.exr",
      "mip1_pz": "prefiltered/mip1_pz.exr",
      "mip1_nz": "prefiltered/mip1_nz.exr"
    }
  },
  "diffuseSh9": "diffuse_sh9.json",
  "radianceMap": {
    "binding": "RadianceMap",
    "format": "RGBA16Float",
    "extent": 64,
    "mips": 1
  }
}
```

BRDF LUT asset:

```json
{
  "schema": "lxe.brdf-lut.v1",
  "bsdfModel": "standard-ggx-split-sum-v1",
  "binding": "BrdfLut",
  "format": "RGBA16Float",
  "extent": 128,
  "mips": 1,
  "file": "standard-ggx-split-sum-v1.exr"
}
```

Every manifest parser is strict. Unknown fields, missing files, wrong schema,
wrong binding, wrong resource type, and incomplete cubemap faces are fatal
diagnostics.

## Runtime Rules

Scene load:

```text
scene file path
  -> derive .lxe-bake/<scene-stem>/scene-bake.json
  -> if manifest exists and loads: register bake assets in SceneResourceTable
  -> if manifest missing: leave environment IBL assets absent
  -> if manifest exists but is invalid: scene load fails with diagnostics
```

`bakeScene(scene, options)`:

```text
scan live material contracts for required BRDF LUT models
run BrdfLutBake once per model
run ReflectionFilter for the global sky environment when present
write manifests and EXR/JSON payloads under .lxe-bake/<scene-stem>/
register live ReflectionProbeBakeAsset and BrdfLutAsset objects
```

No runtime path may call the old private `bakeStaticEnvironment()` entry point.

## Asset Writer Boundary

The writer consumes graph output metadata and backend readback payloads. It does
not decide dimensions by duplicating bake settings.

```cpp
struct BakeImageReadback final {
  std::string graphResource;
  std::string binding;
  u32 width = 0;
  u32 height = 0;
  u32 mipLevel = 0;
  u32 faceLayer = 0;
  std::vector<float> rgba32f;
};

struct SceneBakeWriteRequest final {
  std::filesystem::path scenePath;
  std::filesystem::path cacheRoot;
  std::vector<BakeImageReadback> images;
  std::vector<DiffuseSH9> diffuseProjections;
  std::vector<BrdfLutAssetMetadata> brdfLuts;
};
```

The loader returns live runtime assets, not metadata-only placeholders.

## Tests And Audits

Required tests:

- Missing cache means scene loads and IBL resources are absent.
- Invalid existing cache fails scene load.
- Reflection probe manifest rejects missing cubemap face, missing mip, wrong
  schema, wrong binding, and unknown field.
- BRDF LUT manifest rejects wrong BSDF model, wrong binding, missing EXR, and
  unknown field.
- Writer creates a complete `.lxe-bake/<scene-stem>/` tree for a small global
  environment bake.
- Loader registers live `ReflectionProbeBakeAsset` and `BrdfLutAsset`
  resources in `SceneResourceTable`.

Required audits:

```bash
rg -n "lxe.scene-bake.v1|lxe.reflection-probe-bake.v1|lxe.brdf-lut.v1|\\.lxe-bake" src assets docs notes .gitignore
rg -n "bakeStaticEnvironment\\(|IblBakeRenderer" src/backend src/editor src/test
```

## Out Of Scope

- Local reflection probe capture.
- Probe influence volume UI.
- KTX2/DDS containers.
- Incremental cache invalidation by content hash.
- Lightmap bake output.
