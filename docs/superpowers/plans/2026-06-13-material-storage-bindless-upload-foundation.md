# Material Storage Bindless Upload Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `REQ-073-b` so Material v3 source-reflected records flow from `SceneResourceTableUploadView` into backend-consumable bindless-ready table/staging data without using old `SceneGpuMaterialRecord` as material truth.

**Architecture:** Extend material contract reflection with explicit storage fields, pack each material instance into source-reflected bytes, group records by source storage, and expose draw/object references through a `materialRef` table that resolves to `sourceStorageIndex + sourceLocalMaterialIndex`. Register builtin default textures as SceneResourceTable resources and add a backend staging API that consumes upload view tables without requiring the realtime renderer default path to bind them yet.

Texture slot resolution is intentionally strict: source material packing first uses a table-owned texture handle already attached to the material parameter, then parser-recorded canonical dependency URI lookup. If an explicit texture parameter cannot resolve through either path, packing fails; only absent texture parameters use the storage field's default texture semantic.

**Tech Stack:** C++20, CMake/Ninja, LXEngine `core` scene/material types, `infra` material contract reflector, Vulkan shell GPU resource table, integration tests under `src/test/integration`.

---

## File Structure

- Modify `src/core/asset/material_contract.hpp/.cpp`: add storage field reflection types and include them in source signature/layout validation.
- Modify `src/infra/material_loader/material_contract_reflector.cpp`: parse `storageField:` metadata lines from contract sources.
- Modify `assets/shaders/glsl/common/materials/*.contract.glsl`: declare source storage fields explicitly in shader-adjacent contract metadata.
- Modify `src/core/asset/material_contract_packer.hpp/.cpp`: pack `MaterialInstance` envelopes into source-reflected bytes and default texture slots.
- Modify `src/core/scene/scene_resource_table_upload_view.hpp`: add source storage metadata, material refs, and backend staging spans.
- Modify `src/core/scene/scene_gpu_records.hpp/.cpp`: introduce `SceneGpuMaterialRefRecord` and stop treating old `SceneGpuMaterialRecord` as positive Material v3 truth.
- Modify `src/core/scene/scene_resource_table.hpp/.cpp`: register builtin default textures, build source storages/material refs, resolve texture dependencies, and fail fast on invalid material state.
- Modify `src/core/rhi/gpu_resource_table.hpp`: add a small backend-agnostic scene bindless staging report type.
- Modify `src/backend/vulkan/vulkan_gpu_resource_table.hpp/.cpp`: implement staging consumption in the Vulkan shell resource table.
- Modify tests:
  - `src/test/integration/test_material_source_contract.cpp`
  - `src/test/integration/test_scene_resource_upload_view_v2.cpp`
  - `src/test/integration/test_scene_resource_table.cpp`
  - `src/test/integration/test_bindless_indirect_contract.cpp`
  - `src/test/CMakeLists.txt` if a new focused test binary is added.

## Progress

- [x] Task 1: Reflected explicit storage fields (`6db72c40` plan baseline, `ee4a7091` implementation).
- [x] Task 2: Declared storage fields in supported built-in contract sources (`cda59adb`).
- [x] Task 3: Packed source-reflected material bytes (`34ae2f8b`).
- [x] Task 4: Registered builtin default material textures (`f3e8bd3b`).
- [x] Task 5: Built source storages/material refs in upload view; current in-progress commit also updates `test_scene_resource_table` for hard-cut source-contract behavior.
- [x] Task 6: Added negative upload diagnostics for source signature mismatch, source storage layout conflict, and unresolved explicit texture slots.
- [ ] Task 7: Backend scene bindless staging API.
- [ ] Task 8: Final verification and requirement closeout.

## Task 1: Reflect Explicit Storage Fields

**Files:**
- Modify: `src/core/asset/material_contract.hpp`
- Modify: `src/core/asset/material_contract.cpp`
- Modify: `src/infra/material_loader/material_contract_reflector.cpp`
- Test: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add failing tests for storage field reflection**

Add this storage field reflection test:

```cpp
void testReflectsMaterialStorageFields() {
  const std::string source = R"(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// storageField: baseColor vec4 parameter Kd value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter Kd texture defaultTexture=white
// storageField: baseColorChannel channelSelector parameter Kd channel default=rgba
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { LxMaterialSurface s; return s; }
)";
  auto reflected = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://matte.contract.glsl"), source);
  EXPECT(reflected.diagnostics.empty(), "storage field reflection should pass");
  EXPECT(reflected.reflection.has_value(), "reflection should be produced");
  EXPECT(reflected.reflection->storageFields.size() == 3,
         "three storage fields should be reflected");
  EXPECT(reflected.reflection->storageFields[0].name == "baseColor",
         "first field name should be baseColor");
}
```

Also add a negative test for duplicate storage field names and unknown storage field type.

- [ ] **Step 2: Run failing test**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: FAIL because `MaterialContractReflection::storageFields` and `storageField:` parsing do not exist.

- [ ] **Step 3: Add storage field model**

Add declarations:

```cpp
enum class MaterialContractStorageFieldType {
  Float,
  Vec4,
  TextureSlot,
  ChannelSelector,
  Flags,
};

enum class MaterialContractStorageInputKind {
  ParameterValue,
  ParameterTexture,
  ParameterChannel,
  Constant,
};

struct MaterialContractStorageField final {
  std::string name;
  MaterialContractStorageFieldType type = MaterialContractStorageFieldType::Float;
  MaterialContractStorageInputKind inputKind =
      MaterialContractStorageInputKind::ParameterValue;
  std::string parameterName;
  std::string defaultTextureSemantic;
  Vec4f defaultValue{0.0f, 0.0f, 0.0f, 0.0f};
  std::string defaultChannel = "rgba";
};
```

Add `std::vector<MaterialContractStorageField> storageFields;` to `MaterialContractReflection`.

- [ ] **Step 4: Parse `storageField:` metadata**

Implement parsing for this grammar:

```text
storageField: <fieldName> <type> parameter <parameterName> <value|texture|channel> [default=<float[,float,float,float]>] [defaultTexture=<white|black|flatNormal>]
```

Only accept field types `float`, `vec4`, `textureSlot`, `channelSelector`, `flags`. Reject duplicates and unknown tokens with diagnostics that include the source URI.

- [ ] **Step 5: Include storage fields in layout equality**

Extend `sameContractLayout()` to compare storage fields. A source signature collision with different storage fields must fail `validateMaterialContractReflectionSet()`.

- [ ] **Step 6: Run test**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/asset/material_contract.hpp src/core/asset/material_contract.cpp src/infra/material_loader/material_contract_reflector.cpp src/test/integration/test_material_source_contract.cpp
git commit -m "Reflect material storage fields"
```

## Task 2: Declare Storage Fields In Contract Sources

**Files:**
- Modify: `assets/shaders/glsl/common/materials/matte.contract.glsl`
- Modify: `assets/shaders/glsl/common/materials/metal.contract.glsl`
- Modify: `assets/shaders/glsl/common/materials/uber.contract.glsl`
- Modify: `assets/shaders/glsl/common/materials/substrate.contract.glsl`
- Test: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add positive reflection checks for built-in contracts**

In the existing contract source test, assert every supported built-in source has non-empty `storageFields` and every field references a declared parameter or a constant.

- [ ] **Step 2: Run failing test**

Run:

```bash
./build/src/test/test_material_source_contract
```

Expected: FAIL because built-in sources do not yet declare `storageField:`.

- [ ] **Step 3: Add storage metadata to supported sources**

For PBR-compatible sources, add lines in this shape:

```glsl
// storageField: baseColor vec4 parameter Kd value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter Kd texture defaultTexture=white
// storageField: metallic float parameter metallic value default=0
// storageField: metallicTexture textureSlot parameter metallic texture defaultTexture=white
// storageField: metallicChannel channelSelector parameter metallic channel default=b
// storageField: roughness float parameter roughness value default=0.5
// storageField: roughnessTexture textureSlot parameter roughness texture defaultTexture=white
// storageField: roughnessChannel channelSelector parameter roughness channel default=g
// storageField: ao float parameter ao value default=1
// storageField: aoTexture textureSlot parameter ao texture defaultTexture=white
// storageField: emissive vec4 parameter emissive value default=0,0,0,0
// storageField: emissiveTexture textureSlot parameter emissive texture defaultTexture=black
// storageField: normalScale float parameter normalScale value default=1
// storageField: normalTexture textureSlot parameter normalmap texture defaultTexture=flatNormal
```

Only include fields for parameters declared by that contract. If a source intentionally does not support a parameter, do not add a storage field for it.

- [ ] **Step 4: Run test**

Run:

```bash
./build/src/test/test_material_source_contract
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add assets/shaders/glsl/common/materials/*.contract.glsl src/test/integration/test_material_source_contract.cpp
git commit -m "Declare material source storage fields"
```

## Task 3: Pack Source-Reflected Bytes Records

**Files:**
- Modify: `src/core/asset/material_contract_packer.hpp`
- Modify: `src/core/asset/material_contract_packer.cpp`
- Test: `src/test/integration/test_material_source_contract.cpp`

- [ ] **Step 1: Add failing packer tests**

Add tests that construct a material with `Kd.value`, `Kd.texture`, `metallic.value`, packed channel metadata, and default texture slots. Assert:

```cpp
EXPECT(result.record.bytes.size() > 0, "record bytes should be packed");
EXPECT(result.record.sourceLocalMaterialIndex == 0,
       "packer should preserve source-local material index");
EXPECT(readU32(result.record.bytes, result.layout.field("baseColorTexture").offset) == 5,
       "base color texture slot should be packed");
```

Use helper readers local to the test:

```cpp
u32 readU32(const std::vector<u8> &bytes, usize offset) {
  u32 value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}
```

- [ ] **Step 2: Run failing test**

Run:

```bash
cmake --build build --target test_material_source_contract
./build/src/test/test_material_source_contract
```

Expected: FAIL because packer only preserves source signature/default slots.

- [ ] **Step 3: Extend pack input and output**

Change pack input to include:

```cpp
struct MaterialContractPackInput final {
  const MaterialInstance *material = nullptr;
  MaterialContractReflection contract;
  MaterialContractDefaultTextureSlots defaultTextureSlots;
  u32 sourceLocalMaterialIndex = u32_max;
  std::function<u32(std::string_view)> textureSlotForParameter;
  std::function<u32(const ResourceUri &)> textureSlotForUri;
};
```

Change record metadata to:

```cpp
struct SourceLocalMaterialRecord final {
  u32 sourceLocalMaterialIndex = u32_max;
  std::vector<u8> bytes;
};
```

- [ ] **Step 4: Implement field packing**

Implement deterministic std430-like packing for the reflected field list:

- `float` writes 4 bytes.
- `vec4` writes 16 bytes.
- `textureSlot` writes `u32`.
- `channelSelector` writes `u32` using `r=0`, `g=1`, `b=2`, `a=3`, `rgb=4`, `rgba=5`.
- `flags` writes `u32`.

Missing texture fields use `defaultTexture=<semantic>` slot. Missing required values fail with diagnostics. Optional missing values use field defaults.

- [ ] **Step 5: Run test**

Run:

```bash
./build/src/test/test_material_source_contract
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/asset/material_contract_packer.hpp src/core/asset/material_contract_packer.cpp src/test/integration/test_material_source_contract.cpp
git commit -m "Pack source reflected material records"
```

## Task 4: Add Builtin Default Texture Resources

**Files:**
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`

- [ ] **Step 1: Add failing default texture tests**

Add a test that calls `buildUploadView()` on a scene with a constant-only source material and asserts:

```cpp
EXPECT(table.findTexture(ResourceUri("builtin://textures/default/white")).has_value(),
       "white default texture should have stable identity");
EXPECT(view.textures.size() >= 3, "default textures should enter texture table");
```

Also assert `white`, `black`, and `flat-normal` are deduped across multiple materials.

- [ ] **Step 2: Run failing test**

Run:

```bash
cmake --build build --target test_scene_resource_upload_view_v2
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: FAIL because builtin default textures are not registered by table build.

- [ ] **Step 3: Implement builtin default texture registration**

Add helpers:

```cpp
enum class SceneBuiltinDefaultTexture { White, Black, FlatNormal };

TextureHandle SceneResourceTable::ensureBuiltinDefaultTexture(
    SceneBuiltinDefaultTexture texture) const;
```

Use URIs:

```text
builtin://textures/default/white
builtin://textures/default/black
builtin://textures/default/flat-normal
```

Create 1x1 `CombinedTextureSampler` resources with RGBA values `(255,255,255,255)`, `(0,0,0,255)`, and `(128,128,255,255)`. Register once and return the existing handle on later calls.

- [ ] **Step 4: Run test**

Run:

```bash
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/scene/scene_resource_table.hpp src/core/scene/scene_resource_table.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp
git commit -m "Register default material textures in scene table"
```

## Task 5: Build Source Storages And Material Refs In Upload View

**Files:**
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/scene_gpu_records.hpp`
- Modify: `src/core/asset/material_contract_packer.hpp`
- Modify: `src/core/asset/material_contract_packer.cpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`
- Test: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Add failing tests for sourceStorageIndex/materialRef**

Add assertions:

```cpp
EXPECT(view.materialRefs.size() == view.draws.size(),
       "draws should reference material refs");
EXPECT(view.materialRefs[view.draws[0].materialRefIndex].sourceStorageIndex == 0,
       "draw should resolve to first source storage");
EXPECT(view.materialRefs[view.draws[0].materialRefIndex].sourceLocalMaterialIndex == 0,
       "draw should resolve to first record in storage");
```

Also assert `view.materials` is not used by source-contract positive tests.

- [ ] **Step 2: Run failing test**

Run:

```bash
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: FAIL because `materialRefs` and `materialRefIndex` do not exist.

- [ ] **Step 3: Add material ref records**

Add:

```cpp
struct SceneSourceLocalMaterialStorageView final {
  StringID sourceSignature;
  ResourceUri sourceUri;
  std::string reflectionHash;
  std::string storageAbiHash;
  u32 recordOffset = 0;
  u32 recordCount = 0;
};

struct alignas(16) SceneGpuMaterialRefRecord final {
  u32 sourceStorageIndex = u32_max;
  u32 sourceLocalMaterialIndex = u32_max;
  u32 reserved0 = 0;
  u32 reserved1 = 0;
};

struct alignas(16) SceneGpuDrawRecord final {
  u32 objectIndex = 0;
  u32 materialIndex = u32_max;
  u32 meshIndex = 0;
  u32 materialRefIndex = u32_max;
};
```

Expose `std::span<const SceneGpuMaterialRefRecord> materialRefs;` in `SceneResourceTableUploadView`.

The legacy `materialIndex` field stays in the draw record until `REQ-073-e`; source-contract draws must write `materialIndex == u32_max` and a valid `materialRefIndex`.

- [ ] **Step 4: Rewrite material upload grouping**

During `buildUploadView()`:

1. Resolve each live object material.
2. Validate the material has contract reflection and source signature.
3. Allocate or reuse a source storage.
4. Assign a `sourceLocalMaterialIndex` in that storage.
5. Pack the bytes record through `packMaterialContractRecord()`.
6. Create a material ref and set `draw.materialRefIndex`.
7. Resolve texture fields through table-owned parameter texture handles or parser canonical dependency URIs; do not fall back to defaults for explicit unresolved textures.

Do not create new positive-path `SceneGpuMaterialRecord` entries for source-contract materials.

- [ ] **Step 5: Run test**

Run:

```bash
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/asset/material_contract_packer.hpp src/core/asset/material_contract_packer.cpp src/core/scene/scene_resource_table_upload_view.hpp src/core/scene/scene_gpu_records.hpp src/core/scene/scene_resource_table.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp src/test/integration/test_scene_resource_table.cpp
git commit -m "Build source local material refs in upload view"
```

## Task 6: Add Negative Upload Diagnostics

**Files:**
- Modify: `src/core/scene/scene_resource_table.cpp`
- Test: `src/test/integration/test_scene_resource_upload_view_v2.cpp`

- [ ] **Step 1: Add failing negative tests**

Cover these cases:

```cpp
expectBuildUploadThrows(tableWithMissingSourceSignature,
                        "material source signature");
expectBuildUploadThrows(tableWithSourceLayoutConflict,
                        "source signature conflict");
expectBuildUploadThrows(tableWithUnresolvedTextureDependency,
                        "texture dependency");
expectBuildUploadThrows(tableWithMissingDefaultTextureSlot,
                        "default texture");
```

- [ ] **Step 2: Run failing test**

Run:

```bash
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: FAIL for missing diagnostics.

- [ ] **Step 3: Implement fail-fast checks**

In `buildUploadView()`:

- collect all material contract reflections used by live objects and call `validateMaterialContractReflectionSet()`;
- reject missing contract reflection or empty source signature;
- reject unresolved texture dependencies before packing;
- require default texture slots before calling packer;
- propagate packer diagnostics as `std::logic_error` messages with material URI/source URI context.

- [ ] **Step 4: Run test**

Run:

```bash
./build/src/test/test_scene_resource_upload_view_v2
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/scene/scene_resource_table.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp
git commit -m "Fail fast on invalid material upload records"
```

## Task 7: Backend Scene Bindless Staging API

**Files:**
- Modify: `src/core/rhi/gpu_resource_table.hpp`
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.hpp`
- Modify: `src/backend/vulkan/vulkan_gpu_resource_table.cpp`
- Test: `src/test/integration/test_bindless_indirect_contract.cpp`

- [ ] **Step 1: Add failing backend consumption test**

Construct a small `SceneResourceTable`, build upload view, then call:

```cpp
VulkanGpuResourceTable gpu;
const SceneBindlessUploadReport report = gpu.uploadSceneBindlessTables(view);
EXPECT(report.textureSlots.size() >= 3,
       "default textures should create bindless slots");
EXPECT(report.materialStorageBuffers.size() == view.sourceMaterialStorages.size(),
       "each source storage should create material storage staging");
EXPECT(report.diagnostics.empty(), "valid upload should not diagnose errors");
```

- [ ] **Step 2: Run failing test**

Run:

```bash
cmake --build build --target test_bindless_indirect_contract
./build/src/test/test_bindless_indirect_contract
```

Expected: FAIL because `uploadSceneBindlessTables()` does not exist.

- [ ] **Step 3: Define report types**

Add to `gpu_resource_table.hpp`:

```cpp
struct SceneBindlessTextureSlot final {
  u32 textureTableIndex = u32_max;
  GpuBindlessSlot slot;
};

struct SceneBindlessMaterialStorage final {
  u32 sourceStorageIndex = u32_max;
  GpuBufferHandle buffer;
  u64 byteSize = 0;
};

struct SceneBindlessUploadReport final {
  std::vector<SceneBindlessTextureSlot> textureSlots;
  std::vector<SceneBindlessMaterialStorage> materialStorageBuffers;
  GpuBufferHandle objectBuffer;
  GpuBufferHandle drawBuffer;
  GpuBufferHandle meshBuffer;
  std::vector<std::string> diagnostics;
};
```

Add a virtual method:

```cpp
[[nodiscard]] virtual SceneBindlessUploadReport
uploadSceneBindlessTables(const SceneResourceTableUploadView &view) = 0;
```

- [ ] **Step 4: Implement Vulkan shell staging**

In `VulkanGpuResourceTable`:

- create one descriptor table;
- create image/sampler handles for every `view.textures` entry and update bindless slots;
- create buffers for each source storage bytes range;
- create buffers for objects/draws/meshes/indices/positions;
- record diagnostics for invalid storage ranges or invalid material refs.

- [ ] **Step 5: Run test**

Run:

```bash
./build/src/test/test_bindless_indirect_contract
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/rhi/gpu_resource_table.hpp src/backend/vulkan/vulkan_gpu_resource_table.hpp src/backend/vulkan/vulkan_gpu_resource_table.cpp src/test/integration/test_bindless_indirect_contract.cpp
git commit -m "Stage scene bindless upload tables"
```

## Task 8: Final Requirement Verification

**Files:**
- Modify: `notes/requirements/073-b-material-storage-bindless-upload-foundation.md`
- Test: existing integration tests

- [ ] **Step 1: Run targeted tests**

Run:

```bash
cmake --build build --target test_material_source_contract test_scene_resource_upload_view_v2 test_bindless_indirect_contract test_scene_resource_table
./build/src/test/test_material_source_contract
./build/src/test/test_scene_resource_upload_view_v2
./build/src/test/test_bindless_indirect_contract
./build/src/test/test_scene_resource_table
```

Expected: all pass.

- [ ] **Step 2: Run auto suite**

Run:

```bash
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
```

Expected: PASS. If unrelated failures exist, record exact test names and diagnostics in the final notes update.

- [ ] **Step 3: Audit old material truth in 073-b positive tests**

Run:

```bash
rg -n "SceneGpuMaterialRecord|MaterialUBO|toGpuMaterialRecord|\\.materials" src/test/integration/test_material_source_contract.cpp src/test/integration/test_scene_resource_upload_view_v2.cpp src/test/integration/test_bindless_indirect_contract.cpp
```

Expected: no positive 073-b test uses old `SceneGpuMaterialRecord` / `MaterialUBO` as material truth. Legacy mentions must be named as legacy or negative assertions.

- [ ] **Step 4: Update implementation status**

Set `notes/requirements/073-b-material-storage-bindless-upload-foundation.md` implementation status to completed, listing:

- source-reflected storage fields;
- strict bytes record packing;
- builtin default textures;
- source storage/material ref upload view;
- backend bindless table staging;
- fail-fast diagnostics.

- [ ] **Step 5: Commit final status**

```bash
git add notes/requirements/073-b-material-storage-bindless-upload-foundation.md
git commit -m "Close material storage bindless foundation"
```

## Self-Review

- Spec coverage: Tasks 1-3 cover source-reflected bytes record packing; Task 4 covers default textures; Tasks 5-6 cover upload view, sourceStorageIndex/sourceLocalMaterialIndex, and fail-fast diagnostics; Task 7 covers backend/GPU table staging; Task 8 covers final verification and status.
- Scope check: The plan does not implement shader source variants, URI migration, RenderWorkQueue indirect batching, renderer default-path consumption, or realtime hard cut; those remain in `REQ-073-c/d/e`.
- Placeholder scan: The plan contains no placeholder sections or open-ended error-handling instructions.
