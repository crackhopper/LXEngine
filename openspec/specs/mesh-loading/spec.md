## Purpose

Define the current mesh-loading contract for OBJ and related mesh ingestion paths used by the renderer.

## Requirements

### Requirement: OBJ mesh loading
The mesh loading system SHALL provide an `ObjLoader` class capable of loading Wavefront OBJ files.

#### Scenario: Load valid OBJ file
- **WHEN** `ObjLoader::load("model.obj")` is called on a valid OBJ file
- **THEN** the file SHALL be parsed by tinyobjloader and positions, normals, and texture coordinates SHALL be extracted

#### Scenario: OBJ file not found
- **WHEN** `load()` is called with a non-existent OBJ file
- **THEN** a `std::runtime_error` SHALL be thrown with an appropriate message

### Requirement: OBJ mesh data access
The ObjLoader SHALL provide access to loaded mesh data including vertices, normals, and indices.

#### Scenario: Get vertex positions
- **WHEN** `getPositions()` is called after loading
- **THEN** a vector of `Vec3f` vertex positions SHALL be returned

#### Scenario: Get vertex normals
- **WHEN** `getNormals()` is called after loading
- **THEN** a vector of `Vec3f` vertex normals SHALL be returned

#### Scenario: Get vertex texture coordinates
- **WHEN** `getTexCoords()` is called after loading
- **THEN** a vector of `Vec2f` texture coordinates SHALL be returned

#### Scenario: Get indices
- **WHEN** `getIndices()` is called after loading
- **THEN** a vector of unsigned integer indices SHALL be returned

### Requirement: GLTF mesh loading

The mesh loading system SHALL provide a `GLTFLoader` class capable of loading glTF 2.0 files. Parsing SHALL be implemented by the vendored `cgltf` library, not by any stub. The loader SHALL support ASCII `.gltf` with external `.bin` and external image references as its primary path; `.glb` MAY also work via cgltf's own auto-detection but is not a primary acceptance target. The loader SHALL throw `std::runtime_error` on parse failure with a message that contains the input filename.

#### Scenario: Load valid GLTF file

- **WHEN** `GLTFLoader::load("model.gltf")` is called on a valid glTF 2.0 file whose `meshes[0].primitives[0]` is triangles and has POSITION
- **THEN** the file SHALL be parsed via `cgltf_parse_file` + `cgltf_load_buffers` and geometry + PBR metadata SHALL be extracted from `meshes[0].primitives[0]`

#### Scenario: Load stub is removed

- **WHEN** `GLTFLoader::load` is called on a valid `.gltf` file
- **THEN** the call SHALL NOT throw `"ASCII GLTF (.gltf) not yet supported"` or `"Binary GLTF (.glb) not yet supported"` — those stub messages SHALL NOT appear in the implementation

### Requirement: GLTF mesh data access

The `GLTFLoader` SHALL provide access to loaded mesh data, including the same geometry interface as `ObjLoader` plus glTF-specific PBR extensions.

The public header `src/infra/mesh_loader/gltf_mesh_loader.hpp` SHALL declare, in addition to the position/normal/texCoord/indices accessors:

```cpp
const std::vector<LX_core::Vec4f>& getTangents() const;
const GLTFPbrMaterial&             getMaterial() const;
```

`getTangents()` SHALL return an empty vector when the glTF file does not declare a `TANGENT` accessor for the consumed primitive; the loader SHALL NOT generate tangents automatically. `getMaterial()` SHALL return a `GLTFPbrMaterial` describing the primitive's bound glTF material.

#### Scenario: Get GLTF vertex positions

- **WHEN** `getPositions()` is called after loading a glTF file
- **THEN** a vector of `Vec3f` vertex positions SHALL be returned

#### Scenario: Get tangents when present

- **WHEN** `getTangents()` is called after loading a glTF file that declares a `TANGENT` accessor
- **THEN** a non-empty vector of `Vec4f` tangents SHALL be returned

#### Scenario: Get tangents when absent

- **WHEN** `getTangents()` is called after loading a glTF file with no `TANGENT` accessor
- **THEN** an empty vector SHALL be returned and no tangent generation SHALL have occurred

#### Scenario: Get PBR material metadata

- **WHEN** `getMaterial()` is called after loading
- **THEN** a `GLTFPbrMaterial` SHALL be returned whose factors and texture URI strings reflect the primitive's glTF material (or struct defaults if no material is bound)

### Requirement: Model file path convention
Model test assets SHALL be located under `assets/models/` instead of the root-level `models/` directory. Any code or documentation referencing `models/viking_room.obj` SHALL be updated to reference `assets/models/viking_room/viking_room.obj`.

#### Scenario: Viking room model at new path
- **WHEN** loading the viking_room model
- **THEN** the file path SHALL resolve to `assets/models/viking_room/viking_room.obj`

#### Scenario: Old models directory does not exist
- **WHEN** checking the repository root
- **THEN** no `models/` directory SHALL exist at the root level

### Requirement: Loaded meshes preserve local-space bounds
Mesh ingestion paths SHALL compute and preserve a local-space `BoundingBox`
for loaded geometry. The computed bounds SHALL reflect the loaded vertex
positions in mesh-local space and SHALL be stored on the resulting `Mesh`.

This requirement SHALL apply to both OBJ and GLTF mesh-loading flows, and
SHALL be satisfied during the existing position-reading pass rather than by an
editor-only post-process.

#### Scenario: OBJ mesh load computes bounds
- **WHEN** an OBJ mesh is loaded successfully
- **THEN** the resulting `Mesh` SHALL carry a valid local-space `BoundingBox`
  spanning the loaded positions

#### Scenario: GLTF mesh load computes bounds
- **WHEN** a GLTF mesh is loaded successfully
- **THEN** the resulting `Mesh` SHALL carry a valid local-space `BoundingBox`
  spanning the loaded positions
