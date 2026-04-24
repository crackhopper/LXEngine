## 1. Dependencies

- [x] 1.1 Add yaml-cpp via FetchContent to `src/infra/CMakeLists.txt`
- [x] 1.2 Verify yaml-cpp builds and links successfully

## 2. Placeholder Textures

- [x] 2.1 Create `src/infra/texture_loader/placeholder_textures.hpp` with `getWhite()`, `getBlack()`, `getNormal()` returning `CombinedTextureSamplerSharedPtr`
- [x] 2.2 Create `src/infra/texture_loader/placeholder_textures.cpp` with lazy singleton implementation
- [x] 2.3 Add source to CMakeLists.txt INFRA_SOURCES

## 3. Generic Material Loader

- [x] 3.1 Create `src/infra/material_loader/generic_material_loader.hpp` with `loadGenericMaterial(std::filesystem::path yamlPath)` signature
- [x] 3.2 Implement YAML parsing: read shader name, variants, parameters, resources, passes
- [x] 3.3 Implement shader resolution and per-pass compilation with merged variants
- [x] 3.4 Implement template building: create MaterialTemplate with pass entries and build binding cache
- [x] 3.5 Implement parameter application: parse `bindingName.memberName` keys, validate against reflection, write via setParameter/legacy setters
- [x] 3.6 Implement resource application: resolve placeholders or load from file, bind via setTexture
- [x] 3.7 Implement per-pass overrides: apply pass-level parameters and resources after globals
- [x] 3.8 Add source to CMakeLists.txt INFRA_SOURCES and link yaml-cpp

## 4. Example Material Asset

- [x] 4.1 Create `materials/blinnphong_lit.mat.yaml` as a reference example matching blinnphong_0 shader

## 5. Tests

- [x] 5.1 Add integration test: `loadGenericMaterial` with a valid yaml produces a working MaterialInstance
- [x] 5.2 Add integration test: yaml with placeholder texture resolves correctly
- [x] 5.3 Verify existing tests still pass

## 6. Documentation

- [x] 6.1 Update `notes/subsystems/material-system.md` with generic loader section
- [x] 6.2 Sync specs via openspec archive workflow
