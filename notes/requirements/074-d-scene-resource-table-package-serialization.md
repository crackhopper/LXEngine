# REQ-074-d: SceneResourceTable Package Serialization And Restore

> 2026-06-12 新增：本 REQ 在 `REQ-074-c` 的 package 文件格式之上，实现 `SceneResourceTable` canonical state 的序列化和 restore。目标是从 package 直接恢复 CPU scene resource table，不重新解析 source scene/material/mesh YAML。

## 背景

package 文件格式本身不能提升加载速度，除非 CPU resource state 能从 package section 直接恢复。BMW M6 的加载成本包含 scene YAML、material、mesh、texture metadata、dependency graph 和 typed arrays 的重建。本 REQ 聚焦 CPU 侧 restore，不处理 Vulkan pipeline cache。

## 目标

1. 将 `SceneResourceTable` persisted state 写入 `.lxpkg` sections。
2. 从 package restore resource metadata、dependency graph、typed arrays 和 scene object relationships。
3. package restore 不读取 source scene/material/mesh YAML。
4. source parse 和 package restore 生成一致的 SceneResourceTable root hash。
5. 恢复后的 table 可继续构建 upload view / RenderPathGraph / FrameGraph / render work。
6. 恢复后的 table 可被 realtime 和 OfflineRT 共用。

## 需求

### R1: Persisted State Serialization

SHALL serialize:

- resource metadata。
- canonical URI table。
- dependency graph。
- RenderPathGraph resources/references。
- OfflineRT RenderPathGraph resources/references。
- material envelope、`bsdf.source` URI、source reflection hash/signature and explicit realtime PBR extension parameter state。
- source-reflected material grouping metadata required for restore。
- geometry streams。
- mesh descriptors。
- texture metadata and payload references。
- compressed texture payload metadata produced by `REQ-074-a`。
- scene objects。
- cameras。
- lights。

SHALL NOT serialize:

- GPU handles。
- bindless slots。
- descriptor sets。
- dirty flags。
- FrameGraph compile result。
- render work queues。

### R2: Package Restore

loader SHALL:

1. read package index / metadata / dependencies。
2. restore `SceneResourceTable` resource entries。
3. restore typed arrays and resource handles。
4. fix up object -> mesh/material/camera/light/render path/offline render path references。
5. expose diagnostics with original source URI and package resource id。

If source files are unavailable, restore must still succeed if package payload is complete.

### R3: Streaming Section Restore

large sections SHOULD restore by section/chunk.

Rules:

- loader must not read the whole package into memory as normal path。
- chunk hash mismatch fatal。
- completed texture/geometry chunks can become upload candidates later。
- pending references store compact descriptors, not full package bytes。

### R4: SceneResourceTable Root Hash

Implement deterministic root hash over persisted state.

Hash includes:

- resource metadata。
- canonical URI / stable resource ids。
- dependency edges。
- typed payload hashes。
- object relationships。
- material state, including `bsdf.source` URI and source reflection hash。
- RenderPathGraph references。

Hash excludes:

- GPU handles。
- bindless slots。
- FrameGraph compile result。
- dirty/runtime generation counters。
- unordered iteration order。

### R5: Rebuild Package Input

package build SHALL detect source resource changes by content hash and report added/removed/changed resources.

## 测试

### T1: Small Scene Round Trip

Package and restore a small scene; assert resource count, dependencies, objects and typed arrays match.

### T2: Source Files Unavailable

Move or hide source YAML/material/mesh after package build; restore still succeeds from package.

### T2.1: Texture Payload Restore

Move or hide source PNG/JPEG/EXR after package build; restore texture metadata and compressed payload from package, then upload without rereading the source image.

### T3: Root Hash Match

source parse root hash equals package restore root hash.

### T4: Root Hash Diff

Modify a material parameter, texture hash or dependency edge in package test fixture; root hash mismatch reports subtree/resource.

### T5: Derived State Excluded

Change runtime dirty flags or rebuild upload view order; root hash remains stable.

## 修改范围

- `src/core/package/`
- `src/core/scene/scene_resource_table*`
- resource metadata and handles
- package tests
- BMW small package fixture

## 边界与约束

- 不处理 Vulkan pipeline cache import/export；由 `REQ-074-e` 处理。
- 不实现 editor loading UI。
- 不要求 cross-platform package compatibility。

## 依赖

- `REQ-074-c`: LxScenePackage file format。
- `REQ-073-h`: OfflineRT config hard cut and smoke。

## 后续工作

- `REQ-074-e`: GPU pipeline cache package metadata and Vulkan restore。

## 实施状态

未实施。
