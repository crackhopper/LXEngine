## ADDED Requirements

### Requirement: cdToWhereAssetsExist function
`src/core/utils/filesystem_tools.hpp` SHALL declare and `filesystem_tools.cpp` SHALL implement:

```cpp
bool cdToWhereAssetsExist(const std::string& subpath);
```

#### Scenario: Find existing asset by subpath
- **WHEN** calling `cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")` from a working directory at or below the repository root
- **THEN** the function SHALL return `true` and change cwd to the directory containing `assets/<subpath>`

#### Scenario: Asset not found
- **WHEN** calling `cdToWhereAssetsExist("nonexistent/foo.bar")`
- **THEN** the function SHALL return `false` and NOT change cwd

### Requirement: Upward search behavior
The function SHALL search from the current working directory upward through parent directories, looking for `<dir>/assets/<subpath>`. The search depth SHALL be the same as `cdToWhereShadersExist()` (currently 8 levels).

#### Scenario: Search from nested build directory
- **WHEN** calling `cdToWhereAssetsExist("env/studio_small_03_2k.hdr")` from `<repo>/build/tests/`
- **THEN** the function SHALL find `<repo>/assets/env/studio_small_03_2k.hdr` and return `true`

#### Scenario: Search does not traverse unbounded
- **WHEN** calling from a directory more than 8 levels below any `assets/` directory
- **THEN** the function SHALL return `false` without infinite traversal

### Requirement: CMake build-time asset sync
The top-level `CMakeLists.txt` SHALL create a symlink from `${CMAKE_BINARY_DIR}/assets` to `${CMAKE_SOURCE_DIR}/assets`. If symlink creation fails (e.g., Windows without permissions), it SHALL fall back to `file(COPY ...)`.

#### Scenario: Symlink created on Linux
- **WHEN** running CMake on Linux
- **THEN** `${CMAKE_BINARY_DIR}/assets` SHALL be a symlink pointing to `${CMAKE_SOURCE_DIR}/assets`

#### Scenario: Copy fallback on symlink failure
- **WHEN** symlink creation fails
- **THEN** the build system SHALL copy `assets/` into `${CMAKE_BINARY_DIR}/assets`

### Requirement: Integration test for asset layout
A new test file `src/test/integration/test_assets_layout.cpp` SHALL be created and registered in `src/test/CMakeLists.txt`.

#### Scenario: Positive path - DamagedHelmet
- **WHEN** the test calls `cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")`
- **THEN** the result SHALL be `true`

#### Scenario: Positive path - HDR environment map
- **WHEN** the test calls `cdToWhereAssetsExist("env/studio_small_03_2k.hdr")`
- **THEN** the result SHALL be `true`

#### Scenario: Positive path - viking_room
- **WHEN** the test calls `cdToWhereAssetsExist("models/viking_room/viking_room.obj")`
- **THEN** the result SHALL be `true`

#### Scenario: Negative path - nonexistent asset
- **WHEN** the test calls `cdToWhereAssetsExist("nonexistent/foo.bar")`
- **THEN** the result SHALL be `false`

### Requirement: cdToWhereAssetsExist as fallback
Even if the CMake build-time sync fails or is not run, `cdToWhereAssetsExist()` SHALL still be able to locate assets by traversing upward from any working directory within the repository tree.

#### Scenario: No symlink, helper still works
- **WHEN** `${CMAKE_BINARY_DIR}/assets` does not exist and the test runs from `${CMAKE_BINARY_DIR}`
- **THEN** `cdToWhereAssetsExist("models/viking_room/viking_room.obj")` SHALL return `true` by finding the source-tree `assets/`

### Requirement: Legacy cwd-changing helpers are compatibility fallbacks
`cdToWhereAssetsExist()` and `cdToWhereResourcesCouldFound()` SHALL remain compatibility helpers, but they MUST NOT be the primary runtime asset-root discovery contract for maintained startup paths.

#### Scenario: maintained startup path uses explicit runtime root
- **WHEN** a maintained executable or test initializes runtime asset lookup
- **THEN** it uses the explicit runtime root helper contract first, and only relies on cwd-changing helpers as fallback compatibility behavior
