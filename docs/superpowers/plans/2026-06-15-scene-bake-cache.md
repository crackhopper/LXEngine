# Scene Bake Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist stage 1 bake outputs next to the scene file and load them into `SceneResourceTable` on future scene opens.

**Architecture:** Add strict manifest loaders/writers for scene bake, reflection probe bake, and BRDF LUT assets. Scene loading derives a `.lxe-bake/<scene-stem>/` cache path; missing cache disables IBL resources while preserving direct lighting, and invalid existing cache fails with diagnostics.

**Tech Stack:** C++20, CMake/Ninja, JSON or yaml-cpp manifest parsing, existing EXR image IO, Vulkan readback payloads from stage 1, `SceneResourceTable`, editor scene runtime.

---

## Dependency

Complete `docs/superpowers/plans/2026-06-15-reflection-filter-brdf-graph.md`
first.

## File Structure

- Modify `.gitignore`: ignore `**/.lxe-bake/`.
- Create `src/infra/bake/scene_bake_cache.hpp`.
- Create `src/infra/bake/scene_bake_cache_loader.cpp`.
- Create `src/infra/bake/scene_bake_cache_writer.cpp`.
- Modify `src/infra/CMakeLists.txt`: add bake cache sources.
- Modify `src/editor/runtime/scene_runtime.cpp`: load cache before registering
  environment resources.
- Modify `src/core/scene/scene_resource_table.*`: register
  `ReflectionProbeBakeAsset` and `BrdfLutAsset`.
- Create `src/test/integration/test_scene_bake_cache.cpp`.
- Modify `src/test/CMakeLists.txt`: add the new test.

## Task 1: Add Manifest Loader Tests

**Files:**
- Create: `src/test/integration/test_scene_bake_cache.cpp`
- Modify: `src/test/CMakeLists.txt`

- [ ] **Step 1: Add rejection tests**

Add tests:

```cpp
void testSceneBakeManifestRejectsWrongSchema();
void testReflectionProbeManifestRejectsMissingFace();
void testReflectionProbeManifestRejectsUnknownField();
void testBrdfLutManifestRejectsWrongBinding();
void testBrdfLutManifestRejectsMissingImage();
```

Each test writes a small invalid manifest under
`build/test-artifacts/scene_bake_cache/` and expects the loader to throw
`std::runtime_error` with a message containing the invalid key or schema.

- [ ] **Step 2: Run and verify failure**

Run:

```bash
cmake --build build --target test_scene_bake_cache
./build/src/test/test_scene_bake_cache
```

Expected: build or tests fail before implementation.

## Task 2: Implement Strict Manifest Loading

**Files:**
- Create: `src/infra/bake/scene_bake_cache.hpp`
- Create: `src/infra/bake/scene_bake_cache_loader.cpp`
- Modify: `src/infra/CMakeLists.txt`
- Modify: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] **Step 1: Add data structs**

```cpp
namespace LX_infra::bake {

struct SceneBakeManifest final {
  std::filesystem::path scenePath;
  std::filesystem::path globalReflectionManifest;
  std::map<std::string, std::filesystem::path> brdfLutManifests;
};

struct ReflectionProbeBakeManifest final {
  bool global = true;
  std::string source;
  std::filesystem::path prefilteredManifestRoot;
  std::filesystem::path diffuseSh9Path;
  std::optional<std::filesystem::path> radianceManifestRoot;
};

struct BrdfLutManifest final {
  std::string bsdfModel;
  std::string binding;
  u32 extent = 0;
  std::filesystem::path file;
};

} // namespace LX_infra::bake
```

- [ ] **Step 2: Parse strict schemas**

Accept only:

```text
lxe.scene-bake.v1
lxe.reflection-probe-bake.v1
lxe.brdf-lut.v1
```

Reject unknown root keys and missing listed files.

- [ ] **Step 3: Run loader tests**

Run:

```bash
cmake --build build --target test_scene_bake_cache
./build/src/test/test_scene_bake_cache
```

Expected: rejection tests pass.

## Task 3: Implement Writer

**Files:**
- Modify: `src/infra/bake/scene_bake_cache.hpp`
- Create: `src/infra/bake/scene_bake_cache_writer.cpp`
- Modify: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] **Step 1: Add writer request**

```cpp
struct SceneBakeWriteRequest final {
  std::filesystem::path scenePath;
  std::filesystem::path cacheRoot;
  std::vector<BakeImageReadback> images;
  std::vector<DiffuseSH9> diffuseProjections;
  std::vector<BrdfLutAssetMetadata> brdfLuts;
};

[[nodiscard]] SceneBakeManifest
writeSceneBakeCache(const SceneBakeWriteRequest &request);
```

- [ ] **Step 2: Write payloads**

Write EXR files with names:

```text
environment/prefiltered/mip0_px.exr
environment/prefiltered/mip0_nx.exr
brdf/standard-ggx-split-sum-v1.exr
```

Write SH coefficients to:

```text
environment/diffuse_sh9.json
```

- [ ] **Step 3: Add round-trip test**

The test writes a complete small cache, loads it back, and asserts all manifest
paths are relative to `.lxe-bake/<scene-stem>/`.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_scene_bake_cache
./build/src/test/test_scene_bake_cache
```

Expected: round trip passes.

## Task 4: Register Loaded Assets In SceneResourceTable

**Files:**
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/infra/bake/scene_bake_cache_loader.cpp`
- Modify: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] **Step 1: Add registration API**

```cpp
ReflectionProbeBakeHandle
addReflectionProbeBakeAsset(ReflectionProbeBakeAsset asset);

BrdfLutAssetHandle
addBrdfLutAsset(BrdfLutAsset asset);
```

- [ ] **Step 2: Ensure live payloads**

The loader must construct live texture resources for `PrefilteredEnvMap` and
`BrdfLut`, plus a live `DiffuseSH9` resource. If any payload cannot be loaded,
the loader throws instead of registering metadata-only assets.

- [ ] **Step 3: Add assertions**

Assert:

```cpp
EXPECT(table.getReflectionProbeBakeAssets().size() == 1,
       "one global reflection bake asset should register");
EXPECT(table.getBrdfLutAssets().size() == 1,
       "one BRDF LUT asset should register");
```

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_scene_bake_cache
./build/src/test/test_scene_bake_cache
```

Expected: loaded assets register with live resources.

## Task 5: Load Cache During Scene Runtime

**Files:**
- Modify: `src/editor/runtime/scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] **Step 1: Derive default cache path**

For scene path `dir/name.scene.yaml`, derive:

```text
dir/.lxe-bake/name/scene-bake.json
```

- [ ] **Step 2: Apply load rules**

Implement:

```text
manifest missing -> scene loads; IBL resources absent
manifest present and valid -> load assets into SceneResourceTable
manifest present and invalid -> scene load fails
```

- [ ] **Step 3: Run runtime tests**

Run:

```bash
cmake --build build --target test_scene_bake_cache lxe_editor
./build/src/test/test_scene_bake_cache
```

Expected: editor builds; cache load rules pass.

## Task 6: Add Global Environment bakeScene Entry

**Files:**
- Create: `src/editor/runtime/scene_bake_service.hpp`
- Create: `src/editor/runtime/scene_bake_service.cpp`
- Modify: `src/editor/runtime/scene_runtime.cpp`
- Modify: `src/test/integration/test_scene_bake_cache.cpp`

- [ ] **Step 1: Add API**

```cpp
struct SceneBakeOptions final {
  std::filesystem::path cacheRoot;
  bool bakeGlobalEnvironment = true;
};

SceneBakeResult bakeScene(SceneRuntime &runtime,
                          const SceneBakeOptions &options);
```

- [ ] **Step 2: Run stage 1 graph executors**

For the global environment, execute `ReflectionFilter`. For material contracts,
execute `BrdfLutBake` for each required BSDF LUT model.

- [ ] **Step 3: Write cache and register assets**

Call `writeSceneBakeCache(request)`, then load the written cache back into the
active `SceneResourceTable`.

- [ ] **Step 4: Run tests**

Run:

```bash
cmake --build build --target test_scene_bake_cache test_vulkan_ibl_bake
./build/src/test/test_scene_bake_cache
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

Expected: `bakeScene` writes a cache and the current runtime has live global IBL
assets after bake.

## Task 7: Final Verification

**Files:**
- Modify: `assets/shaders/README.md`
- Modify: `notes/concepts/material/shader-catalog.md`

- [ ] **Step 1: Run audits**

Run:

```bash
rg -n "lxe.scene-bake.v1|lxe.reflection-probe-bake.v1|lxe.brdf-lut.v1|\\.lxe-bake" src assets docs notes .gitignore
rg -n "bakeStaticEnvironment\\(|IblBakeRenderer" src/backend src/editor src/test
git diff --check
```

Expected: manifest terms appear only in loader/writer/tests/docs; no old direct
runtime bake entry point remains.

- [ ] **Step 2: Run final build/test set**

Run:

```bash
cmake --build build --target test_scene_bake_cache test_vulkan_ibl_bake lxe_editor
./build/src/test/test_scene_bake_cache
xvfb-run -a ./build/src/test/test_vulkan_ibl_bake
```

Expected: all commands exit 0.
