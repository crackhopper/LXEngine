## Purpose

Define the repository asset directory layout, baseline test assets, size budget, and documentation expectations for checked-in runtime assets.

## Requirements

### Requirement: Assets directory structure
The repository SHALL have a top-level `assets/` directory with the following sub-directories: `models/`, `textures/`, `env/`.

#### Scenario: Directory exists after implementation
- **WHEN** a developer clones the repository
- **THEN** the directory structure `assets/models/`, `assets/textures/`, `assets/env/` SHALL exist

### Requirement: Minimum test asset baseline
The `assets/` directory SHALL contain the following test assets:

| Asset | Location | Purpose |
|---|---|---|
| DamagedHelmet | `assets/models/damaged_helmet/` | PBR 主测试模型 |
| Sponza | `assets/models/sponza/` | Shadow / 多 mesh / culling 压力场景 |
| Stanford Bunny | `assets/models/stanford_bunny/` | 经典 baseline 模型 |
| viking_room.obj | `assets/models/viking_room/` | 兼容旧 demo |
| viking_room.png | `assets/textures/viking_room/` | 兼容旧 demo |
| studio_small_03_2k.hdr | `assets/env/` | IBL 环境贴图输入 |

#### Scenario: All baseline assets present
- **WHEN** a developer lists the contents of `assets/`
- **THEN** all assets listed in the baseline table SHALL be present

#### Scenario: DamagedHelmet contains glTF and textures
- **WHEN** a developer inspects `assets/models/damaged_helmet/`
- **THEN** the directory SHALL contain `DamagedHelmet.gltf`, `DamagedHelmet.bin`, and associated texture files

### Requirement: Total asset size budget
The total size of all files under `assets/` SHALL NOT exceed 100 MB.

#### Scenario: Size within budget
- **WHEN** the total size of `assets/` is computed
- **THEN** the result SHALL be less than or equal to 100 MB

#### Scenario: Stanford Bunny causes budget overflow
- **WHEN** adding Stanford Bunny would cause total size to exceed 100 MB
- **THEN** the implementation SHALL first try a lighter representation, and if still exceeding, remove bunny and document the reason in README.md

### Requirement: Asset trimming priority
DamagedHelmet, Sponza, studio_small_03_2k.hdr, and viking_room assets SHALL NOT be removed under any trimming scenario. Stanford Bunny is the first candidate for removal.

#### Scenario: Trimming preserves core assets
- **WHEN** trimming is required to meet the 100 MB budget
- **THEN** DamagedHelmet, Sponza, HDR, and viking_room SHALL remain; only Stanford Bunny MAY be removed

### Requirement: Old root directories removed
After migration, the root-level `models/` and `textures/` directories SHALL be deleted. All references SHALL point to `assets/`.

#### Scenario: Root models directory removed
- **WHEN** the migration is complete
- **THEN** `models/` at repository root SHALL NOT exist

#### Scenario: Root textures directory removed
- **WHEN** the migration is complete
- **THEN** `textures/` at repository root SHALL NOT exist

### Requirement: Asset README per directory
Each top-level asset directory (e.g., `assets/models/damaged_helmet/`) SHALL contain a `README.md` with at least: asset name, source URL, original license, purpose in this repository, key file list, and file size.

#### Scenario: DamagedHelmet README present and complete
- **WHEN** a developer reads `assets/models/damaged_helmet/README.md`
- **THEN** it SHALL contain the asset name, source URL, license, purpose, file list, and size

### Requirement: Assets overview README
`assets/README.md` SHALL contain a summary of all assets, total size statistics, trimming principles, and which downstream requirements consume these assets.

#### Scenario: Overview README is accurate
- **WHEN** a developer reads `assets/README.md`
- **THEN** the listed total size SHALL match the actual size of `assets/`

### Requirement: Prohibited assets
The repository SHALL NOT include 4K or higher HDR files, Bistro, Cornell Box, or other large scenes beyond the baseline.

#### Scenario: No 4K HDR present
- **WHEN** scanning `assets/` for HDR files
- **THEN** no file SHALL exceed 2K resolution

### Requirement: No external download or submodule dependencies
Assets SHALL be committed directly to the repository. The implementation SHALL NOT use git submodules, download scripts, or external package managers for asset acquisition.

#### Scenario: Offline clone is self-contained
- **WHEN** a developer clones the repository without network access
- **THEN** all test assets SHALL be immediately available under `assets/`
