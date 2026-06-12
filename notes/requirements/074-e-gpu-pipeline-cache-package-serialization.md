# REQ-074-e: GPU Pipeline Cache Package Metadata And Vulkan Restore

> 2026-06-12 新增：本 REQ 在 `REQ-074-d` 的 CPU package restore 之上，实现 GPU pipeline cache metadata 和 Vulkan pipeline cache blob 的 package 保存/恢复。目标是减少 BMW M6 等场景加载时 shader/pipeline 重新编译成本。

## 背景

SceneResourceTable package restore 只能避免 CPU source parsing。首次渲染仍可能因为 shader reflection、pipeline build desc collection、Vulkan pipeline creation 和 driver cache miss 卡顿。Vulkan 提供 pipeline cache blob 能力，本 REQ 将其作为 optional backend cache section 接入 package。由于 `REQ-073-d` / `REQ-073-e` 已经让 OfflineRT compute pass 也走 RenderPathGraph / FrameGraph / RenderWorkItem，pipeline cache metadata 需要同时覆盖 realtime raster pipeline 和 OfflineRT compute pipeline。

## 目标

1. GPUResourceTable / Vulkan backend 导出 pipeline cache metadata 和 blob。
2. package 保存 backend cache compatibility key、pipeline key list 和 cache blob。
3. package restore 后尝试导入 compatible pipeline cache。
4. cache mismatch 时 warning 并 rebuild，不破坏 CPU package restore。
5. 记录 pipeline cache 对 BMW M6 load/preload time 的影响，为 `REQ-074-f` 准备数据。

## 需求

### R1: Backend Cache Metadata

package SHALL save optional backend cache metadata:

- backend name。
- backend version。
- GPU/driver/cache compatibility key。
- shader/pipeline key list。
- render path / pass references。
- compute pass references and dispatch/profile variant metadata when the pipeline belongs to OfflineRT。
- pipeline cache blob section references。

### R2: Vulkan Pipeline Cache Export

Vulkan backend SHALL export driver pipeline cache blob after pipeline preload/build.

Requirements:

- export failure records diagnostic。
- empty cache is allowed but must be explicit。
- blob hash is stored in package backend section。

### R3: Vulkan Pipeline Cache Import

On package restore:

- CPU package restore happens first。
- backend compatibility key is checked。
- compatible cache blob is imported before pipeline preload。
- mismatch warns and rebuilds pipeline cache。
- corrupt blob warns/fails according to severity but does not corrupt CPU scene state。

### R4: Pipeline Build Desc Collection

FrameGraph / RenderPathGraph compile SHALL produce deterministic pipeline build desc list for cache preload.

Rules:

- use RenderPathGraph / RenderPath terms。
- pipeline key includes structural facts only。
- material parameter values do not create pipeline identity。
- OfflineRT compute pipeline descs come from the same graph/pass/work-item path as realtime descs, not from `offlineShader` or hardcoded pass names。

### R5: Cache Observability

Tests and logs SHALL expose:

- cache imported or skipped。
- cache compatibility result。
- pipeline cache hit/miss/preload count。
- pipeline build time with and without cache。

## 测试

### T1: Cache Blob Round Trip

Build pipelines, export cache blob, import into fresh backend table, preload same pipeline descs.

### T2: Compatibility Mismatch

Change compatibility key; restore warns and rebuilds.

### T3: Corrupt Blob

Corrupt cache blob; restore reports diagnostic and does not corrupt CPU package restore.

### T4: Deterministic Desc List

Same package restore produces same pipeline key/build desc list.

## 修改范围

- Vulkan pipeline cache / GPUResourceTable
- `src/core/package/` backend cache sections
- RenderPathGraph pipeline desc collection
- package restore tests

## 边界与约束

- 不 serialize GPU handles or descriptor sets。
- CPU package must remain valid without backend cache。
- 不实现 editor loading UI。

## 依赖

- `REQ-074-c`: LxScenePackage file format。
- `REQ-074-d`: SceneResourceTable package serialization and restore。
- `REQ-073-b`: bindless/indirect material path hard cut。
- `REQ-073-e`: OfflineRT config hard cut and smoke。

## 后续工作

- `REQ-074-f`: BMW M6 package load performance comparison。

## 实施状态

未实施。
