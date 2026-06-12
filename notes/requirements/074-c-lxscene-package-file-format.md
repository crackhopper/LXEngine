# REQ-074-c: LxScenePackage File Format

> 2026-06-12 新增：本 REQ 定义第一版 `.lxpkg` / LxScenePackage 二进制文件格式。它只定义容器、section、chunk、hash 和 compatibility metadata，不实现完整 SceneResourceTable restore 或 Vulkan pipeline cache restore。

## 背景

BMW M6 这类场景加载慢，主要来自大量源文件解析、纹理处理、shader/pipeline 准备和资源关系重建。要优化加载速度，首先需要一个稳定的本机二进制 package 格式，把已经解析和编码后的资源 payload 按 section/chunk 存储起来。

`REQ-074-a` 已准备 texture compression payload，`REQ-074-b` 确保 package 前 canonical data readiness，其中包括 realtime 与 OfflineRT 都已经走 RenderPathGraph 默认路径。本 REQ 在此基础上定义文件格式。

## 目标

1. 定义单文件二进制 LxScenePackage container。
2. 定义 header、section table、chunk table 和 package index。
3. 定义 CPU resource sections 与 optional backend cache sections 的边界。
4. 定义 hash、alignment、compatibility key 和 rebuild diagnostic。

## 需求

### R1: Package Header

package SHALL 使用固定 header。

最低字段：

```text
magic
schemaVersion
nativeAbiKey
engineBuildKey
packageFlags
headerSize
sectionTableOffset
sectionCount
packageIndexSection
packageHash
```

不匹配的 `magic`、`schemaVersion`、`nativeAbiKey` 或必要 feature flag SHALL fail-fast，并提示 rebuild package。

### R2: Section Table

section table SHALL 描述每个 top-level section。

最低字段：

```text
type
offset
byteSize
alignment
contentHash
restorePhase
dependencyMask
chunkTableOffset
chunkCount
```

section offset 必须满足 alignment。loader 先读 header + section table，再决定读取哪些 section。

### R3: Chunk Table

大型 section SHALL 支持 chunk table。

chunk 字段：

```text
offset
byteSize
resourceIdRange or typedIndexRange
contentHash
alignment
uploadHint
```

geometry、texture、material instance storage 等大型 payload 可以按 chunk 并行读取。小型 section 可以没有 chunk table。

### R4: Section Type Registry

第一版格式 SHALL 至少定义以下 section types。CPU restore 所需 sections 是必需的；backend cache sections 只是 optional section type，本 REQ 只定义其容器位置，不实现 Vulkan cache restore。

| Section | 内容 |
|---|---|
| PackageIndex | source URI、resource list、section references |
| StringTable / UriTable | 字符串、URI、diagnostic text |
| ResourceMetadata | resource type、canonical URI、content hash、state |
| DependencyGraph | stable resource id edges |
| RenderPathGraphs | RenderPathGraph declarations / references |
| MaterialRecords | material persisted state / grouping references |
| GeometryStreams | position/index/attribute payload |
| MeshDescriptors | mesh/primitive metadata |
| TexturePayloads | decoded/compressed texture payload and metadata |
| SceneObjects | object -> mesh/material/camera/light/effect relations |
| Cameras | camera state |
| Lights | light state |
| BackendCacheMetadata | optional backend cache index, implemented by `REQ-074-e` |
| BackendCacheBlobs | optional Vulkan pipeline cache blobs, implemented by `REQ-074-e` |

### R5: Persisted vs Derived Boundary

format SHALL distinguish persisted canonical data from derived runtime data.

Persisted:

- canonical resource metadata。
- dependency graph。
- material envelopes and explicit realtime PBR extension parameters。
- geometry streams。
- texture payloads, including BC7 compressed payload from `REQ-074-a`。
- scene object relationships。
- RenderPathGraph assets/references。
- OfflineRT RenderPathGraph assets/references。

Derived, not package truth:

- GPU handles。
- bindless slots。
- descriptor sets。
- runtime dirty flags。
- frame graph compile cache。
- render work queue。
- transient upload staging buffers。

### R6: Compatibility Metadata

package SHALL include:

- engine build key。
- native ABI key。
- package schema version。
- optional backend name / driver / GPU cache compatibility key。

CPU package can load without backend cache. Backend cache mismatch should not invalidate CPU package by itself.

## 测试

### T1: Header And Section Parse

Write a minimal package, read header/section table, reject bad magic/version/alignment/hash.

### T2: Chunk Table Parse

Create a texture payload section with multiple chunks; verify offsets, hashes and resource ranges.

### T3: Persisted/Derived Audit

Verify package format has no field for GPU handle, descriptor set, runtime pointer or dirty flag.

## 修改范围

- `src/core/package/`
- `src/core/resource/resource_metadata.*`
- package writer/reader tests
- docs / requirements references for package terminology

## 边界与约束

- 不 restore SceneResourceTable yet。
- 不 import Vulkan pipeline cache yet。
- 不 implement loading UI。
- 格式第一版是本机 engine package，不承诺跨平台长期兼容。

## 依赖

- `REQ-074-a`: BC7 texture compression payload metadata。
- `REQ-074-b`: package canonical state readiness gate。

## 后续工作

- `REQ-074-d`: SceneResourceTable package serialization and restore。
- `REQ-074-e`: GPU pipeline cache metadata and Vulkan restore。

## 实施状态

未实施。
