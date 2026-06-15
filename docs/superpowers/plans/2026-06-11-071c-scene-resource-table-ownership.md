# 071-c SceneResourceTable Ownership Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `SceneResourceTable` the real owner of scene resources by moving mesh/texture/effect/camera/light/material-instance outputs into typed table storage with dependency, dirty/version, override identity, and bindless upload-view mappings.

**Architecture:** Implement this as a sequence of clean ownership cuts. First tighten the resource table core so every resource has metadata, state, version, dependencies, and typed-index mapping. Then convert parsers from “intern metadata only” to “return handles for payloads owned by `SceneResourceTable`”. Finally make material overrides create distinct envelope-aware `MaterialInstance` resources and export a stable upload view that maps `ResourceHandle -> typed index` for later GPU bindless table upload.

**Tech Stack:** C++20, CMake/Ninja, yaml-cpp, existing LXEngine `src/core/scene`, `src/infra/resource_parsers`, `src/infra/material_loader`, and integration tests under `src/test/integration`.

---

## Scope

This plan implements the next 071-c slice requested by the user:

- `SceneResourceTable` owns typed resources instead of loaders/parsers directly mutating scattered objects.
- `MeshResourceParser` and `TextureResourceParser` fill typed storage and dependencies.
- Add parser/table coverage for camera, light, render-effect, spectrum, and bsdf-table resources.
- Add or complete resource state/version/dirty propagation.
- Make material overrides envelope-aware and immutable with respect to base material/template.
- Export stable `ResourceHandle -> typed index` upload view for bindless-friendly CPU arrays.

This plan does not implement GPU upload/bindless descriptors from REQ-071-d, binary scene package output from REQ-071-e, or renderer-wide draw submission migration. Existing transitional draw paths may remain only if they consume the new table-owned resource data.

---

## File Structure

- Modify: `src/core/scene/scene_resource_table.hpp`
  - Define/complete typed storage accessors, resource state/version, dependency graph, dirty propagation, material override registration, and upload view entry points.

- Modify: `src/core/scene/scene_resource_table.cpp`
  - Implement canonical-handle ownership, typed payload insertion, state transitions, dependency registration, downstream dirty marking, and package-ready graph export.

- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
  - Extend upload view to expose stable typed arrays and `ResourceHandle -> typed index` maps for textures, spectra, bsdf tables, geometry streams, mesh descriptors, material instances, objects, cameras, lights, and effects.

- Modify: `src/core/resource/resource_metadata.hpp`
  - Ensure `ResourceType`, `ResourceState`, version, diagnostics, and dependency metadata are explicit and shared.

- Modify: `src/infra/resource_parsers/mesh_resource_parser.hpp`
- Modify: `src/infra/resource_parsers/mesh_resource_parser.cpp`
  - Store mesh/geometry payloads in table-owned typed storage.

- Modify: `src/infra/resource_parsers/texture_resource_parser.hpp`
- Modify: `src/infra/resource_parsers/texture_resource_parser.cpp`
  - Store texture payloads and metadata in table-owned typed storage.

- Modify: `src/infra/resource_parsers/render_effect_resource_parser.hpp`
- Modify: `src/infra/resource_parsers/render_effect_resource_parser.cpp`
  - Return/register effect handles in the table and record shader/resource dependencies.

- Create or modify: `src/infra/resource_parsers/camera_resource_parser.hpp`
- Create or modify: `src/infra/resource_parsers/camera_resource_parser.cpp`
  - Parse camera state/effect references into table-owned camera resources.

- Create or modify: `src/infra/resource_parsers/light_resource_parser.hpp`
- Create or modify: `src/infra/resource_parsers/light_resource_parser.cpp`
  - Parse typed directional/point/spot light payloads into table-owned light resources.

- Create or modify: `src/infra/resource_parsers/spectrum_resource_parser.hpp`
- Create or modify: `src/infra/resource_parsers/spectrum_resource_parser.cpp`
  - Register spectrum resources and dependencies used by material envelopes.

- Create or modify: `src/infra/resource_parsers/bsdf_table_resource_parser.hpp`
- Create or modify: `src/infra/resource_parsers/bsdf_table_resource_parser.cpp`
  - Register BSDF table resources and dependencies used by material envelopes.

- Modify: `src/infra/material_loader/material_resource_parser.cpp`
  - Make material dependencies refer to table-owned texture/spectrum/bsdf-table handles.

- Modify: `src/infra/scene_asset/scene_material_loader.cpp`
- Modify as needed: scene/gltf loader code that currently mutates material instances directly
  - Route material overrides through table-owned override instance registration.

- Modify: `src/infra/CMakeLists.txt`
  - Add any new parser source files.

- Tests:
  - Modify/add: `src/test/integration/test_scene_resource_table.cpp`
  - Modify/add: `src/test/integration/test_material_v2_resource_dependencies.cpp`
  - Modify/add: `src/test/integration/test_gltf_scene_asset_loader.cpp`
  - Modify/add: `src/test/integration/test_scene_document.cpp`
  - Modify/add: `src/test/integration/test_071_bridge_audit.cpp`
  - Verify: `src/test/integration/test_bindless_validation_contract.cpp`

---

## Task 1: Establish Table Ownership And Resource State Tests

**Files:**
- Modify: `src/test/integration/test_scene_resource_table.cpp`
- Modify: `src/core/resource/resource_metadata.hpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`

- [ ] **Step 1: Add failing test for typed payload ownership**

Add a test that registers a texture and mesh through `SceneResourceTable`, destroys local parser-owned temporaries, then reads payloads back through handles:

```cpp
void testSceneResourceTableOwnsTypedPayloads() {
  SceneResourceTable table;
  const ResourceUri textureUri("memory://texture/shared-white");
  TextureResource texture;
  texture.width = 1;
  texture.height = 1;
  texture.format = TextureFormat::RGBA8;

  const ResourceHandle textureHandle =
      table.registerTexture(textureUri, std::move(texture));

  const ResourceUri meshUri("memory://mesh/triangle");
  MeshResource mesh;
  mesh.positions = {{0.0f, 0.0f, 0.0f},
                    {1.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f}};
  mesh.indices = {0, 1, 2};

  const ResourceHandle meshHandle = table.registerMesh(meshUri, std::move(mesh));

  EXPECT(textureHandle.isValid(), "texture handle should be valid");
  EXPECT(meshHandle.isValid(), "mesh handle should be valid");
  EXPECT(table.texture(textureHandle).width == 1,
         "table should own texture payload");
  EXPECT(table.mesh(meshHandle).indices.size() == 3,
         "table should own mesh payload");
}
```

- [ ] **Step 2: Add failing test for state/version/dirty propagation**

Add:

```cpp
void testResourceStateVersionAndDirtyPropagation() {
  SceneResourceTable table;
  const ResourceHandle texture =
      table.registerTexture(ResourceUri("memory://texture/albedo"),
                            TextureResource{});
  const ResourceHandle material =
      table.registerMaterialInstance(ResourceUri("memory://material/base"),
                                     MaterialInstance{});
  table.addDependency(material, texture);

  const auto before = table.metadata(material).version;
  table.markDirty(texture, "texture reloaded");

  EXPECT(table.metadata(texture).state == ResourceState::Ready,
         "dirty ready resource should remain ready");
  EXPECT(table.metadata(texture).version > 0,
         "dirty resource should bump version");
  EXPECT(table.metadata(material).version > before,
         "dependent material should receive dirty version bump");
  EXPECT(table.metadata(material).diagnostics.back().find("texture reloaded") !=
             std::string::npos,
         "dirty propagation should keep reason");
}
```

- [ ] **Step 3: Run and confirm RED**

Run:

```bash
cmake --build build --target test_scene_resource_table
./build/src/test/test_scene_resource_table
```

Expected: compile failure or test failure because typed ownership/state/dirty APIs are incomplete.

- [ ] **Step 4: Implement resource metadata core**

In `resource_metadata.hpp`, ensure the shared metadata supports:

```cpp
enum class ResourceState {
  Unloaded,
  Loading,
  Ready,
  Failed,
  Dirty,
};

struct ResourceMetadata {
  ResourceType type = ResourceType::Unknown;
  ResourceUri uri;
  ResourceState state = ResourceState::Unloaded;
  u64 generation = 0;
  u64 version = 0;
  std::vector<ResourceHandle> dependencies;
  std::vector<ResourceHandle> dependents;
  std::vector<std::string> diagnostics;
};
```

- [ ] **Step 5: Implement typed storage ownership**

In `SceneResourceTable`, add typed vectors and registration/accessors for at least:

```cpp
std::vector<TextureResource> m_textures;
std::vector<MeshResource> m_meshes;
std::vector<MaterialInstance> m_materialInstances;
std::unordered_map<ResourceHandle, usize> m_textureIndexByHandle;
std::unordered_map<ResourceHandle, usize> m_meshIndexByHandle;
std::unordered_map<ResourceHandle, usize> m_materialInstanceIndexByHandle;
```

Implement:

```cpp
ResourceHandle registerTexture(const ResourceUri &uri, TextureResource texture);
ResourceHandle registerMesh(const ResourceUri &uri, MeshResource mesh);
ResourceHandle registerMaterialInstance(const ResourceUri &uri,
                                        MaterialInstance instance);
const TextureResource &texture(ResourceHandle handle) const;
const MeshResource &mesh(ResourceHandle handle) const;
const MaterialInstance &materialInstance(ResourceHandle handle) const;
```

If equivalent types already exist, reuse the local names and add only missing ownership/index maps.

- [ ] **Step 6: Implement dependencies and dirty propagation**

Add:

```cpp
void addDependency(ResourceHandle owner, ResourceHandle dependency);
void markDirty(ResourceHandle handle, std::string reason);
const ResourceMetadata &metadata(ResourceHandle handle) const;
```

`markDirty()` must bump the resource version and recursively bump dependents. Use a visited set to avoid cycles.

- [ ] **Step 7: Run test**

Run:

```bash
cmake --build build --target test_scene_resource_table
./build/src/test/test_scene_resource_table
```

Expected: PASS.

---

## Task 2: Convert MeshResourceParser And TextureResourceParser To Fill Typed Storage

**Files:**
- Modify: `src/test/integration/test_scene_resource_table.cpp`
- Modify: `src/infra/resource_parsers/mesh_resource_parser.cpp`
- Modify: `src/infra/resource_parsers/mesh_resource_parser.hpp`
- Modify: `src/infra/resource_parsers/texture_resource_parser.cpp`
- Modify: `src/infra/resource_parsers/texture_resource_parser.hpp`

- [ ] **Step 1: Add parser ownership tests**

Add:

```cpp
void testMeshAndTextureParsersReturnTableOwnedHandles() {
  SceneResourceTable table;
  MeshResourceParser meshParser;
  TextureResourceParser textureParser;

  const ResourceHandle texture = textureParser.parse(
      table, ResourceUri("assets://textures/test-white.png"), ParseContext{});
  const ResourceHandle mesh = meshParser.parse(
      table, ResourceUri("assets://meshes/triangle.obj"), ParseContext{});

  EXPECT(texture.isValid(), "texture parser should return table handle");
  EXPECT(mesh.isValid(), "mesh parser should return table handle");
  EXPECT(table.metadata(texture).type == ResourceType::Texture,
         "texture handle should identify texture resource");
  EXPECT(table.metadata(mesh).type == ResourceType::Mesh,
         "mesh handle should identify mesh resource");
  EXPECT(table.hasTexture(texture), "table should own texture payload");
  EXPECT(table.hasMesh(mesh), "table should own mesh payload");
}
```

Use existing test assets if the exact paths differ; do not add parser-private storage to satisfy this test.

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target test_scene_resource_table
./build/src/test/test_scene_resource_table
```

Expected: failure where parsers currently only intern metadata or do not return table-owned payload handles.

- [ ] **Step 3: Update parser interfaces to return `ResourceHandle`**

Use this shape, matching existing parser registry conventions if already present:

```cpp
ResourceHandle MeshResourceParser::parse(SceneResourceTable &table,
                                         const ResourceUri &uri,
                                         const ParseContext &context);

ResourceHandle TextureResourceParser::parse(SceneResourceTable &table,
                                            const ResourceUri &uri,
                                            const ParseContext &context);
```

- [ ] **Step 4: Store mesh geometry payload in the table**

`MeshResourceParser` must:

- Resolve canonical URI through `SceneResourceTable`.
- Return an existing handle if the canonical URI was already loaded.
- Load mesh data.
- Fill position/index data.
- Fill optional attribute stream metadata for normal/uv/tangent/color when present.
- Register the payload with `table.registerMesh()`.

Do not silently create default normal/uv/tangent streams.

- [ ] **Step 5: Store texture payload in the table**

`TextureResourceParser` must:

- Resolve canonical URI through `SceneResourceTable`.
- Return an existing handle if already loaded.
- Load texture bytes/metadata.
- Register payload with `table.registerTexture()`.
- Record diagnostics on failure in table metadata.

- [ ] **Step 6: Run tests**

Run:

```bash
cmake --build build --target test_scene_resource_table test_material_v2_resource_dependencies
./build/src/test/test_scene_resource_table
./build/src/test/test_material_v2_resource_dependencies
```

Expected: PASS.

---

## Task 3: Add Camera, Light, RenderEffect, Spectrum, And BsdfTable Parsers

**Files:**
- Modify/add parser files listed in File Structure.
- Modify: `src/infra/CMakeLists.txt`
- Modify/add: `src/test/integration/test_scene_resource_table.cpp`

- [ ] **Step 1: Add failing parser registry test**

Add:

```cpp
void testSceneResourceParsersPopulateAll071cResourceTypes() {
  SceneResourceTable table;
  CameraResourceParser cameraParser;
  LightResourceParser lightParser;
  RenderEffectResourceParser effectParser;
  SpectrumResourceParser spectrumParser;
  BsdfTableResourceParser bsdfTableParser;

  const ResourceHandle camera = cameraParser.parse(
      table, ResourceUri("memory://camera/main"), ParseContext{});
  const ResourceHandle light = lightParser.parse(
      table, ResourceUri("memory://light/key"), ParseContext{});
  const ResourceHandle effect = effectParser.parse(
      table, ResourceUri("assets://effects/tone_mapping.render-effect.yaml"),
      ParseContext{});
  const ResourceHandle spectrum = spectrumParser.parse(
      table, ResourceUri("memory://spectrum/d65"), ParseContext{});
  const ResourceHandle bsdf = bsdfTableParser.parse(
      table, ResourceUri("memory://bsdf/rough-glass"), ParseContext{});

  EXPECT(table.metadata(camera).type == ResourceType::Camera,
         "camera parser should register camera resource");
  EXPECT(table.metadata(light).type == ResourceType::Light,
         "light parser should register light resource");
  EXPECT(table.metadata(effect).type == ResourceType::Effect,
         "render-effect parser should register effect resource");
  EXPECT(table.metadata(spectrum).type == ResourceType::Spectrum,
         "spectrum parser should register spectrum resource");
  EXPECT(table.metadata(bsdf).type == ResourceType::BsdfTable,
         "bsdf-table parser should register bsdf table resource");
}
```

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target test_scene_resource_table
./build/src/test/test_scene_resource_table
```

Expected: missing parser classes or failing typed registration.

- [ ] **Step 3: Implement lightweight parser classes**

Each parser should parse enough current data to create a real typed table payload and return a handle. For memory URI tests, allow a minimal default payload. For file URI tests, parse actual YAML using current helper functions.

Use this pattern:

```cpp
ResourceHandle CameraResourceParser::parse(SceneResourceTable &table,
                                           const ResourceUri &uri,
                                           const ParseContext &context) {
  if (auto existing = table.find(uri, ResourceType::Camera)) {
    return *existing;
  }
  CameraResource camera;
  camera.uri = uri;
  camera.fovY = 60.0f;
  return table.registerCamera(uri, std::move(camera));
}
```

Apply equivalent logic to light, spectrum, bsdf-table, and effect resources. `RenderEffectResourceParser` must store the parsed `RenderEffect` in table-owned storage rather than returning only a temporary parsed struct.

- [ ] **Step 4: Record effect dependencies**

For each render-effect pass shader URI, register or record a dependency edge:

```cpp
table.addDependency(effectHandle,
                    table.internExternalDependency(ResourceType::Shader,
                                                   pass.shaderUri));
```

Use the repo’s existing shader/resource handle convention if one already exists.

- [ ] **Step 5: Build and run**

Run:

```bash
cmake --build build --target test_scene_resource_table test_render_effect_resource_parser
./build/src/test/test_scene_resource_table
./build/src/test/test_render_effect_resource_parser
```

Expected: PASS.

---

## Task 4: Make Material Dependencies Table-Owned

**Files:**
- Modify: `src/test/integration/test_material_v2_resource_dependencies.cpp`
- Modify: `src/infra/material_loader/material_resource_parser.cpp`
- Modify: `src/core/asset/material_instance.hpp`
- Modify: `src/core/asset/material_instance.cpp`

- [ ] **Step 1: Add failing shared texture dependency test**

Add or tighten:

```cpp
void testTwoMaterialsShareOneTableOwnedTextureDependency() {
  SceneResourceTable table;
  MaterialResourceParser parser;

  const ResourceHandle first = parser.parse(
      table, ResourceUri("memory://materials/a"), materialYamlUsing("textures/shared.png"));
  const ResourceHandle second = parser.parse(
      table, ResourceUri("memory://materials/b"), materialYamlUsing("textures/shared.png"));

  const ResourceHandle firstTexture =
      table.dependencies(first, ResourceType::Texture).front();
  const ResourceHandle secondTexture =
      table.dependencies(second, ResourceType::Texture).front();

  EXPECT(firstTexture == secondTexture,
         "same canonical texture URI should produce one texture handle");
  EXPECT(table.uploadView().textures.size() == 1,
         "upload view should contain one typed texture entry");
}
```

Use the existing YAML helper in the test file; if none exists, add a small local helper that emits a valid `.material` v2 with one texture resource.

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target test_material_v2_resource_dependencies
./build/src/test/test_material_v2_resource_dependencies
```

Expected: failure where material dependencies are only metadata or not mapped to typed table handles.

- [ ] **Step 3: Route material resource dependencies through table**

When `MaterialResourceParser` sees texture/spectrum/bsdf-table dependencies:

- Canonicalize the dependency URI via `SceneResourceTable`.
- Use the relevant parser or `internExternalDependency()` to obtain a `ResourceHandle`.
- Store dependency handles on the `MaterialInstance`.
- Add dependency edges from material instance handle to dependency handles.

- [ ] **Step 4: Keep material-owned reflection path intact**

Do not create ad hoc C++ classes for user shader variables. The parser should keep using reflection/resource declarations already established by 071-a/071-b and only change ownership/dependency storage.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target test_material_v2_resource_dependencies test_generic_material_loader
./build/src/test/test_material_v2_resource_dependencies
./build/src/test/test_generic_material_loader
```

Expected: PASS.

---

## Task 5: Make Material Override Envelope-Aware And Immutable

**Files:**
- Modify: `src/test/integration/test_gltf_scene_asset_loader.cpp`
- Modify: `src/test/integration/test_scene_document.cpp`
- Modify: `src/infra/scene_asset/scene_material_loader.cpp`
- Modify: `src/core/scene/scene_resource_table.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`

- [ ] **Step 1: Add failing override identity test**

Add:

```cpp
void testMaterialOverrideCreatesDistinctMaterialInstanceResource() {
  SceneResourceTable table;
  MaterialInstance base;
  base.setMaterialEnvelope(StringID("Kd"),
                           MaterialParameterEnvelope(Vec3f{1.0f, 1.0f, 1.0f}));

  const ResourceUri baseUri("memory://materials/base");
  const ResourceHandle baseHandle =
      table.registerMaterialInstance(baseUri, base);

  MaterialOverridePatch override;
  override.setEnvelope(StringID("Kd"),
                       MaterialParameterEnvelope(Vec3f{0.25f, 0.5f, 0.75f}));

  const ResourceHandle overrideHandle =
      table.registerMaterialOverride(baseHandle, override);

  EXPECT(baseHandle != overrideHandle,
         "override must create a distinct material instance handle");
  EXPECT(table.materialInstance(baseHandle)
             .getMaterialEnvelope(StringID("Kd"))
             ->getVec3()
             .x == 1.0f,
         "base material instance must not be mutated");
  EXPECT(table.materialInstance(overrideHandle)
             .getMaterialEnvelope(StringID("Kd"))
             ->getVec3()
             .x == 0.25f,
         "override instance should carry patched envelope");
}
```

Use the actual envelope getter names from the repo when implementing.

- [ ] **Step 2: Add override dedup test**

Add:

```cpp
void testEquivalentMaterialOverridesReuseInstanceHandle() {
  SceneResourceTable table;
  const ResourceHandle base =
      table.registerMaterialInstance(ResourceUri("memory://materials/base"),
                                     MaterialInstance{});

  MaterialOverridePatch first;
  first.setEnvelope(StringID("Kd"),
                    MaterialParameterEnvelope(Vec3f{0.1f, 0.2f, 0.3f}));
  MaterialOverridePatch second;
  second.setEnvelope(StringID("Kd"),
                     MaterialParameterEnvelope(Vec3f{0.1f, 0.2f, 0.3f}));

  EXPECT(table.registerMaterialOverride(base, first) ==
             table.registerMaterialOverride(base, second),
         "same base material and same override hash should reuse handle");
}
```

- [ ] **Step 3: Run and confirm RED**

Run:

```bash
cmake --build build --target test_scene_resource_table test_gltf_scene_asset_loader
./build/src/test/test_scene_resource_table
./build/src/test/test_gltf_scene_asset_loader
```

Expected: failure because override registration/mutation semantics are incomplete.

- [ ] **Step 4: Implement override patch identity**

Add a compact override patch structure if missing:

```cpp
struct MaterialOverridePatch {
  std::map<StringID, MaterialParameterEnvelope> envelopes;

  void setEnvelope(StringID name, MaterialParameterEnvelope value);
  [[nodiscard]] StringID identityHash() const;
};
```

The identity hash must include base material handle identity and sorted envelope entries. Do not include node name or display name.

- [ ] **Step 5: Implement `registerMaterialOverride()`**

`SceneResourceTable::registerMaterialOverride(baseHandle, patch)` must:

- Read the base `MaterialInstance`.
- Copy it.
- Apply envelope patch to the copy.
- Build URI identity like `baseUri + "#override=" + hash`.
- Return existing handle for same base+hash.
- Register a dependency from override instance to base instance.
- Never mutate the base instance/template.

- [ ] **Step 6: Route scene/gltf material overrides through the table**

Replace direct mutation of loaded material instances in scene/gltf loader code with:

```cpp
ResourceHandle materialForNode =
    table.registerMaterialOverride(baseMaterialHandle, overridePatch);
sceneObject.material = materialForNode;
```

- [ ] **Step 7: Run tests**

Run:

```bash
cmake --build build --target test_scene_resource_table test_gltf_scene_asset_loader test_scene_document
./build/src/test/test_scene_resource_table
./build/src/test/test_gltf_scene_asset_loader
./build/src/test/test_scene_document
```

Expected: PASS.

---

## Task 6: Export Bindless-Friendly Upload View

**Files:**
- Modify: `src/core/scene/scene_resource_table_upload_view.hpp`
- Modify: `src/core/scene/scene_resource_table.cpp`
- Modify: `src/test/integration/test_scene_resource_table.cpp`
- Modify: `src/test/integration/test_071_bridge_audit.cpp`

- [ ] **Step 1: Add failing upload-view test**

Add:

```cpp
void testUploadViewExportsStableHandleToTypedIndexMaps() {
  SceneResourceTable table;
  const ResourceHandle texture =
      table.registerTexture(ResourceUri("memory://textures/a"), TextureResource{});
  const ResourceHandle mesh =
      table.registerMesh(ResourceUri("memory://meshes/a"), MeshResource{});
  const ResourceHandle material =
      table.registerMaterialInstance(ResourceUri("memory://materials/a"),
                                     MaterialInstance{});
  const ResourceHandle object = table.registerSceneObject(
      ResourceUri("memory://objects/a"),
      SceneObjectResource{.mesh = mesh, .materialInstance = material});

  const SceneResourceTableUploadView view = table.exportUploadView();

  EXPECT(view.textureIndex(texture).has_value(),
         "texture handle should map to typed texture index");
  EXPECT(view.meshIndex(mesh).has_value(),
         "mesh handle should map to typed mesh index");
  EXPECT(view.materialInstanceIndex(material).has_value(),
         "material handle should map to typed material index");
  EXPECT(view.objectIndex(object).has_value(),
         "object handle should map to typed object index");
  EXPECT(*view.objectIndex(object) == 0,
         "first object typed index should be stable in snapshot");
}
```

- [ ] **Step 2: Add shared resource dedup assertion**

Add:

```cpp
void testUploadViewDeduplicatesCanonicalTextureUris() {
  SceneResourceTable table;
  const ResourceUri uri("memory://textures/shared");
  const ResourceHandle first = table.registerTexture(uri, TextureResource{});
  const ResourceHandle second = table.registerTexture(uri, TextureResource{});

  const SceneResourceTableUploadView view = table.exportUploadView();

  EXPECT(first == second, "same canonical URI should return same handle");
  EXPECT(view.textures.size() == 1,
         "upload view should contain one typed texture payload");
  EXPECT(view.textureIndex(first) == view.textureIndex(second),
         "same texture handle should map to same typed index");
}
```

- [ ] **Step 3: Run and confirm RED**

Run:

```bash
cmake --build build --target test_scene_resource_table
./build/src/test/test_scene_resource_table
```

Expected: failure where upload view lacks stable typed maps for required resource types.

- [ ] **Step 4: Implement upload view maps**

`SceneResourceTableUploadView` should expose typed arrays and lookup methods:

```cpp
std::vector<TextureResource> textures;
std::vector<MeshResource> meshes;
std::vector<MaterialInstance> materialInstances;
std::vector<SceneObjectResource> objects;
std::vector<CameraResource> cameras;
std::vector<LightResource> lights;
std::vector<RenderEffect> effects;

std::optional<usize> textureIndex(ResourceHandle handle) const;
std::optional<usize> meshIndex(ResourceHandle handle) const;
std::optional<usize> materialInstanceIndex(ResourceHandle handle) const;
std::optional<usize> objectIndex(ResourceHandle handle) const;
```

Keep map construction inside `SceneResourceTable::exportUploadView()` so GPU upload code can consume one snapshot without doing path/name dedup.

- [ ] **Step 5: Ensure object/material/draw indirection is represented**

`SceneObjectResource` or the existing object upload record must contain:

```cpp
ResourceHandle mesh;
ResourceHandle materialInstance;
Mat4f transform;
```

Do not store parser-private object pointers in upload records.

- [ ] **Step 6: Run tests**

Run:

```bash
cmake --build build --target test_scene_resource_table test_071_bridge_audit test_bindless_validation_contract
./build/src/test/test_scene_resource_table
./build/src/test/test_071_bridge_audit
./build/src/test/test_bindless_validation_contract
```

Expected: PASS.

---

## Task 7: Clean Legacy Parser Ownership Paths

**Files:**
- Modify only call sites found by static scan.

- [ ] **Step 1: Static scan**

Run:

```bash
rg -n 'intern.*metadata|register.*metadata|new MaterialInstance|setMaterialEnvelope|setMaterial.*Override|std::make_shared<.*Texture|MeshResourceParser|TextureResourceParser|LegacyPerItem|enginePass|runtimePassForTechniquePass' src/core src/infra src/test -g'*.cpp' -g'*.hpp'
```

Expected review targets:

- Parser code should not create long-lived resources outside `SceneResourceTable`.
- Scene/gltf loaders should not mutate base `MaterialInstance` for node overrides.
- No `enginePass` bridge beyond rejection test/parser diagnostic.
- No new `LegacyPerItem` branch or permissive bindless fallback.

- [ ] **Step 2: Remove dead compatibility functions**

For each obsolete helper proven unused by `rg`, remove it in the same patch as the call-site migration. Do not keep compatibility wrappers unless a current test or production call site still uses them.

- [ ] **Step 3: Run focused 071-c tests**

Run:

```bash
cmake --build build --target \
  test_scene_resource_table \
  test_material_v2_resource_dependencies \
  test_gltf_scene_asset_loader \
  test_scene_document \
  test_071_bridge_audit \
  test_bindless_validation_contract

ctest --test-dir build --output-on-failure -R 'test_(scene_resource_table|material_v2_resource_dependencies|gltf_scene_asset_loader|scene_document|071_bridge_audit|bindless_validation_contract)$'
```

Expected: 100% tests passed.

- [ ] **Step 4: Diff review**

Run:

```bash
git diff -- src/core/scene src/core/resource src/infra/resource_parsers src/infra/material_loader src/infra/scene_asset src/test/integration/test_scene_resource_table.cpp src/test/integration/test_material_v2_resource_dependencies.cpp src/test/integration/test_gltf_scene_asset_loader.cpp
```

Expected:

- `SceneResourceTable` owns typed resource payloads.
- Parsers return handles and register payloads/dependencies through the table.
- Overrides create new material-instance handles.
- Upload view exposes handle-to-typed-index maps.
- No broad renderer rewrite or unrelated asset churn.

---

## Acceptance Criteria

- `MeshResourceParser` and `TextureResourceParser` no longer only intern metadata; they register typed payloads in `SceneResourceTable`.
- Camera, light, render-effect, spectrum, and bsdf-table resource parser paths exist and produce table-owned handles.
- `SceneResourceTable` metadata includes state, version, dependencies, dependents, diagnostics, and dirty propagation.
- Material overrides create distinct envelope-aware `MaterialInstance` resources and do not mutate base material/template.
- Upload view exports stable typed arrays and `ResourceHandle -> typed index` mappings.
- Static scan shows no new legacy parser-owned resource path, `enginePass` bridge, pass-name fallback, or expanded `LegacyPerItem` path.
- Focused 071-c and bindless audit tests pass.

## Spec Coverage Check

- R1/R2: Covered by canonical handle ownership, metadata, typed storage, state/version/dependencies.
- R3/R6: Covered by parser classes returning table-owned handles and moving format-specific parsing out of table core.
- R4: Covered by envelope-aware material override identity and immutable base instance behavior.
- R5/R8: Covered by dependency graph and package-ready metadata/export foundation.
- R9/R10/R11: Covered by mesh typed storage, object/material indirection, and bindless-friendly upload view.
- T1-T7.1: Covered by targeted tests in this plan.
- T8: Not fully executed by this plan; helmet editor/offline smoke remains a later verification gate after the resource ownership migration compiles cleanly.
