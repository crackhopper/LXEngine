# REQ-071-e: Scene Package 快速加载与 MaterialTemplate 组织

> 2026-06-10 新增：本 REQ 是 `REQ-071` 连续需求族的第五步。目标是在 `REQ-071-c` 的 CPU resource graph 和 `REQ-071-d` 的 GPU cache/upload 接口之上，引入第一版 scene package，把解析结果和依赖资源整体打包，减少重复解析和加载卡顿，并为按 MaterialTemplate / BSDF type 组织渲染工作提供数据基础。

## 背景

BMW M6 和后续 helmet/BMW 对齐测试都会暴露加载性能问题：

- 多 mesh、多 material、多 texture、多 effect 会反复解析 YAML、OBJ、shader、texture。
- scene load 期间 editor 容易看起来卡死。
- `SceneResourceTable` 已经能以 URI 管理资源身份，适合导出 resource graph。
- GPUResourceTable 能导入 backend cache 并任务化上传，适合在 package 加载后恢复 backend 状态。

本 REQ 做第一版快速加载 package，不追求复杂压缩、增量 patch 或跨平台资产格式。目标是把“解析结果 + typed resource arrays + 依赖资源 payload + backend cache metadata”打包成一个大的本机二进制容器，加载时先读取 header / section table，再按 section 依赖并行读取，并在 section 到达后流式恢复对应 CPU 结构，最后交给 GPUResourceTable 上传。

## 目标

1. 定义第一版单文件二进制 scene package 格式，例如 `.lxpkg`。
2. 保存 `SceneResourceTable` 的 CPU resource graph、typed arrays 和 parser 解析结果。
3. 内嵌依赖资源 payload 或其已解析二进制快照，避免加载时反复解析 YAML/OBJ/texture metadata。
4. 保存 backend cache metadata / pipeline cache blob。
5. 加载 package 时先读取 header / section table，再并行读取可独立 section；section 读完后立即恢复对应 SceneResourceTable storage，不等待整个 package 全部读入内存。
6. 按 MaterialTemplate / BSDF type 导出组织信息，为 deferred lighting 和 draw/work grouping 提供基础。

## 需求

### R1: Single Binary Package Container

scene package SHALL 是一个单文件二进制容器。外部主格式不是 YAML/JSON manifest；manifest/index 是容器内部 section。

建议文件结构：

```text
LxScenePackageHeader
  magic = "LXPKG"
  schemaVersion
  nativeAbiKey
  engineBuildKey
  headerSize
  sectionTableOffset
  sectionCount
  packageHash

SectionTable[]
  type
  offset
  byteSize
  alignment
  contentHash
  dependencyMask / restorePhase
  chunkTableOffset
  chunkCount

Sections:
  PackageIndex
  StringTable / URI table
  ResourceMetadata
  DependencyGraph
  MaterialTemplates
  MaterialInstancesByTemplate
  GeometryStreams
  MeshDescriptors
  TexturePayloads / ImageMetadata
  SpectrumPayloads
  BsdfTablePayloads
  SceneObjects
  Cameras
  Lights
  RenderEffects
  TechniquePassMetadata
  BackendCacheMetadata
  BackendCacheBlobs
```

容器内部 index SHALL 保存：

- source scene URI。
- package schema version。
- resource list、type、canonical URI、package URI、content hash。
- dependency graph。
- parser diagnostics。
- material template / BSDF type grouping metadata。
- backend cache metadata。

读取规则：

- loader 先读 header 和 section table，再根据 section 类型、依赖和 IO 粒度并行读取需要的数据。
- section table SHALL 记录 section dependency / restore phase，用于区分“可并行读取”和“必须等前置数据恢复后才能重建”的阶段。
- 大型资源 section SHALL 内含 chunk table；顶层 section 负责类型、依赖和 restore phase，chunk 负责具体资源 payload 的并行读取、恢复和上传进度。
- section offset SHALL 满足对齐要求，便于块读取、并行读取或后续 mmap。
- 字符串、URI、诊断文本进入 string table，结构体 section 只保存 offset/index。
- 第一版可以不做压缩；如果后续引入压缩，必须以 section 为单位，不能破坏并行读取、依赖恢复和进度统计。
- package 文件不承诺跨平台可读。`nativeAbiKey` / `engineBuildKey` / backend compatibility metadata 不匹配时应提示 rebuild package，而不是尝试兼容读取。
- loader SHALL NOT 把整个 package 文件一次性读入内存后再重建场景。除 header/index/string table 等小型基础 section 外，大型 geometry/texture/material payload 必须以 section/chunk 为单位边读边恢复。

### R1.1: Section / Chunk Granularity

package SHALL 使用“顶层 section + 内部 chunk”的二级粒度。

规则：

- 顶层 section 按资源类型和恢复阶段组织，例如 `GeometryStreams`、`TexturePayloads`、`MaterialInstancesByTemplate`、`SceneObjects`。
- 大型 section 内部 SHALL 有 chunk table。chunk 记录 offset、byteSize、resource handle/index range、content hash、alignment 和可选 upload hint。
- geometry stream 可以按 stream 或连续 range 分 chunk。
- texture payload 可以按 texture 或 mip chain 分 chunk；首版优先按 texture 分 chunk，除非单个 texture 过大。
- material instance storage 可以按 MaterialTemplate / BSDF type 分 chunk。
- 小型 section 可以没有 chunk table，直接作为一个整体读取/恢复。
- loader 可以并行读取同一 section 的多个独立 chunk；恢复阶段按 chunk 的 resource range 写入 typed storage。
- chunk 完成后即可产生对应 upload subtask；不必等待整个 section 完成。
- section/chunk hash mismatch fatal，除非用户显式 rebuild package。

### R2: CPU Resource Snapshot Serialization

package SHALL 序列化 `SceneResourceTable` 可快速恢复的信息，而不是保存一组源文件再重新解析。

最低要求：

- material v2 parsed parameters、MaterialTemplate 分组、MaterialTechniqueSet、MaterialInstance arrays。
- mesh/geometry metadata、bounds、position/index/attribute stream 二进制数据。
- texture/HDR metadata 与 image payload；首版可保存原始 image payload 加解析后 metadata，但加载路径不得重新解析 scene/material YAML。
- SPD/eta/k/BSDF table payload 或已解析二进制表。
- camera/light/effect definitions 与 technique/pass metadata。
- scene object -> mesh/material/camera/light/effect handles 的关系。
- `REQ-071-c` 定义的 persisted typed resource arrays。

首版 SHALL 生成一个单一 `.lxpkg` 二进制文件。不得以“YAML/JSON manifest + 原始资源目录”作为快速加载路径。

加载 package 时 SHALL 直接重建 `SceneResourceTable` 内存结构，包括 resource metadata、persisted typed storage、dependencies 和 diagnostics。除非用户显式要求 rebuild package，否则不重新读取源 scene/material/mesh YAML。

### R2.1: Persisted State vs Derived Runtime State

package 只序列化需要从 cache 恢复的 canonical state。加载后可直接计算得到的派生结果不进入 package 主快照，也不进入 SceneResourceTable Merkle hash。

分类规则：

| 类别 | 示例 | package 是否保存 | Merkle hash 是否覆盖 |
|---|---|---|---|
| persisted canonical state | resource metadata、canonical URI、dependency graph、material envelope、technique/pass declaration、geometry streams、texture payload、camera/light/effect 定义、scene object 关系 | 是 | 是 |
| persisted acceleration/index state | MaterialTemplate grouping、MaterialInstancesByTemplate、按类型组织的 typed arrays，前提是 package 直接恢复这些结构以避免重新构建 | 是 | 是 |
| derived CPU runtime state | upload view、FrameGraph compile result、pipeline build desc collection、draw work items、dirty flags、runtime generation counters、validation cache | 否，加载后计算 | 否 |
| derived GPU/backend state | GPUResourceTable handle、bindless slot、descriptor set、pipeline object、upload staging buffer | 否；backend cache blob 单独保存 | 否 |
| backend cache payload | Vulkan pipeline cache blob、backend compatibility metadata | 可选单独 section | 不进入 CPU SceneResourceTable root hash |

要求：

- package 中保存的 typed arrays 必须有明确的 persisted contract；如果某个数组只是为了上传或渲染临时生成，它应归为 derived CPU runtime state。
- `ResourceHandle` 的长期身份 SHALL 通过 package 内稳定 resource id / canonical URI 恢复；运行时 handle 的 generation、内存 index 或 freelist 状态不作为 package hash 输入。
- `ResourceHandle -> typed index` 如果作为 package 恢复结果的一部分被持久化，则 hash 覆盖这个稳定映射；如果加载后重新从 persisted arrays 推导，则不记录 hash。
- dirty/version/runtime generation 在 package restore 后重新初始化，不作为 source parse 和 package restore 等价性的判断依据，除非某个 version 明确代表资源内容版本。

恢复规则：

- PackageIndex / StringTable / ResourceMetadata / DependencyGraph 属于基础 section，先恢复。
- MaterialTemplates、MaterialInstances、GeometryStreams、MeshDescriptors、TexturePayloads、SceneObjects、Camera/Light/Effect 等大型 section 读完即可写入对应 typed storage。
- 对带 chunk table 的大型 section，chunk 读完即可写入对应 typed storage；section 完成只表示该类型资源全部恢复完成。
- 若 section 依赖尚未满足，loader 只保留该 section 的最小 pending descriptor，不保留整份 package 数据。
- resource handle fixup 以 section 为粒度执行；依赖满足后立刻把 pending handle/index 修正为 SceneResourceTable 内部 handle。
- CPU restore 可以和 GPU upload task 串流衔接：某类资源 section 恢复完成后即可生成对应 upload task，不必等全部 scene section 都恢复完成。

### R3: Package URI Resolver

加载 package 时 SHALL 使用 package resolver。

规则：

- package 内 URI 优先解析到 package 内 section/resource record。
- canonical URI 和原始 source URI 都保留用于 diagnostics。
- 缺 resource hash 或 hash mismatch 时 fatal，除非用户显式要求 rebuild package。
- 同一 package 内 resource 仍按 URI 去重。
- package resolver 不把 package resource 解包成临时文件再走普通 parser；它应直接返回 package section 中恢复出的 resource handle / payload view。

### R4: Backend Cache Metadata

package SHALL 可保存 GPUResourceTable 导出的 backend cache metadata。

要求：

- backend cache 与 CPU resource graph 解耦；CPU package 可在没有 cache 时加载。
- cache compatibility key 不匹配时 warning 并重新构建 pipeline。
- cache blob 不作为唯一真相；pipeline build desc 和 shader URI 仍在 CPU package 中。

### R5: Package Load Flow

package load SHALL 遵循：

```text
read package header + section table
parallel read independent package sections
streaming restore SceneResourceTable typed storage as sections complete
validate active technique
compile FrameGraph
import backend cache if compatible
create upload/preload tasks
show editor progress/log
activate scene after tasks finish
```

加载失败时，不替换当前 active scene。

性能要求：

- loader SHOULD 以 section 为单位进行大块 IO，避免大量小文件 open/read。
- 可独立 section SHOULD 并行读取；依赖顺序只约束内存重建和 resource handle fixup，不强制所有 bytes 顺序读取。
- 大型 section 内的独立 chunk SHOULD 并行读取和恢复；进度统计同时包含 section progress 与 chunk progress。
- loader SHALL 边读取边重建，不允许以“先把完整 package 读到内存，再统一反序列化”为默认路径。
- 每个 section read / restore SHALL 报告进度，editor loading UI 可以显示当前 section、并行 task 和 restore phase。
- 支持从 package 直接构造 upload view，避免再次遍历源文件格式。

### R6: MaterialTemplate / BSDF Type Grouping

package SHALL 导出按 material template / BSDF type 的分组信息。

用途：

- deferred lighting 可以按 BSDF type/material template 组织 lighting pass。
- diagnostics 可以统计 scene 中 matte/glass/metal/substrate/fourier/mix 数量。
- 后续可在 mesh/material 合批或 draw sorting 中使用。

首版不强制做网格合批。原因：在 G-Buffer 路径中，几何 pass 即使一个 object 一个 draw，光照也可以按 BSDF/material template 组织；过早合并 mesh 会增加编辑器 selection、picking、override 的复杂度。

### R7: SceneResourceTable Merkle Hash

`SceneResourceTable` SHALL 支持快速一致性哈希，用于验证 source parse 和 package restore 后的状态匹配。

要求：

- 每个 persisted resource entry 计算 resource hash，输入包含 resource type、canonical URI、content hash、typed payload hash、dependency stable id 列表和关键 metadata。
- 每个 persisted typed array 计算 array hash，输入包含元素数量、元素内容、稳定 package resource id / typed index 映射和稳定排序后的 resource references。
- dependency graph 计算 graph hash，输入包含稳定排序后的 edge 列表。
- MaterialTemplate grouping、MaterialInstanceByTemplate、object/material/mesh/camera/light/effect persisted arrays 分别计算 subtree hash。
- root hash 由各 subtree hash 组合而成，类似 Merkle tree。
- hash 输入必须使用确定性顺序；不能依赖 unordered container iteration、对象地址、运行时 handle generation、dirty flag、GPU handle、bindless slot、FrameGraph compile result、upload view、线程调度顺序或 package chunk 完成顺序。
- package build 时保存 source parse 后的 SceneResourceTable root hash 与 subtree hash。
- package restore 后重新计算 root hash 与 subtree hash；不匹配时 fatal，并报告第一个不匹配的 subtree/resource URI/type。

该 hash 用于验证 CPU resource table persisted state 恢复正确性，不替代渲染输出测试，也不包含加载后可计算的间接结果或 GPUResourceTable 的 backend handle。

### R8: Rebuild Package

提供 package rebuild 入口。

要求：

- source scene 或依赖资源 hash 改变时可重新生成 package。
- rebuild 重新运行 parser，并更新 manifest。
- diagnostics 中列出新增/删除/变更的资源。

## 测试

### T1: Package Round Trip

用小 scene 打包再加载：

- resource count 一致。
- dependency graph 一致。
- object -> mesh/material 关系一致。
- camera/effect 关系一致。
- package load 不读取 source scene/material YAML。
- loader 按 section 恢复 typed arrays。
- source parse 得到的 SceneResourceTable root hash 与 package restore 后 root hash 一致。

### T1.1: SceneResourceTable Merkle Diff

构造 source parse table 与 package restore table 的故意差异，例如修改某个 material 参数、texture hash 或 dependency edge：

- root hash 不一致。
- diagnostics 能定位到不一致的 subtree。
- 对 resource 差异，diagnostics 包含 resource type、canonical URI、字段或 payload hash。
- hash 计算不依赖 GPU handle、bindless slot、upload view、FrameGraph result、dirty flag、内存地址、unordered container 遍历顺序或 package chunk 完成顺序。

### T1.2: Derived State Excluded From Hash

构造两个 package restore 结果，让它们的 persisted resource state 相同，但 upload view 构建顺序、runtime handle generation、dirty flag 初始值或 FrameGraph compile cache 不同：

- SceneResourceTable root hash 仍一致。
- 重新计算 upload view / FrameGraph 后渲染路径可继续工作。
- 如果 persisted material parameter、geometry stream、dependency edge 或 scene object 关系改变，root hash 必须改变。

### T1.3: Offline Render Equivalence

使用同一个测试 scene，分别从 source parse 和 `.lxpkg` package restore 构建 `SceneResourceTable`，再运行 offline renderer。

要求：

- 两次渲染使用相同 camera、resolution、render settings、active technique / integrator、sample count 和 random seed。
- source parse table 与 package restore table 的 Merkle root hash 必须先一致。
- offline 渲染输出应完全一致；如果输出格式包含时间戳、metadata 或浮点非确定性字段，对比时只比较像素 buffer / deterministic render payload。
- 如果当前 offline backend 存在已知非确定性并导致 bit-exact 不可达，必须先修复或显式记录原因；不能用宽松视觉阈值替代本测试。
- 不一致时 diagnostics SHALL 输出 root hash、subtree hash、render settings hash、random seed、camera URI 和 output diff 摘要。

### T2: URI Resolver

package 内资源与 asset root 有同名文件时，加载 package 应使用 package 内资源。

### T2.1: Single Binary Parallel Section Load

构建 `.lxpkg` 后删除或移动源 scene/material/mesh YAML：

- package 仍可加载。
- loader 只打开 package 单文件。
- section table、content hash 和 typed arrays 可恢复。
- 进度日志包含 section read task、restore phase 和可并行 section 的完成状态。
- 峰值 CPU 临时内存不随 package 文件总大小线性增长；大型 section 读完后进入 typed storage 或 upload staging，不保留完整 package 副本。

### T2.2: Chunked Streaming Restore

构建包含多个 mesh stream 和多张 texture 的 package：

- `GeometryStreams` / `TexturePayloads` section 包含 chunk table。
- loader 能并行读取多个 chunk。
- 单个 chunk 完成后，对应 typed storage 范围可见。
- 对应 GPU upload subtask 可在 chunk 恢复后创建。
- hash mismatch 的 chunk 会阻止 scene 激活并给出 section/chunk/resource 信息。

### T3: Hash Mismatch

篡改 package resource，加载 fatal 或要求 rebuild，不静默使用。

### T4: Backend Cache Fallback

cache compatibility mismatch：

- 输出 warning。
- 重新 pipeline preload。
- scene 仍可加载。

### T5: MaterialTemplate Grouping

BMW/helmet package 输出 BSDF type/material template 分组统计，Deferred lighting 可读取该分组信息。

### T6: Helmet Rendering Smoke Gate

本 REQ 完成时 SHALL 继续运行 helmet editor/offline smoke：

- helmet scene 可从 source scene 加载，也可从 scene package 加载。
- package load 后 editor realtime 输出非全黑。
- package load 后 offline direct 输出非全黑。
- package cache/backend cache mismatch 时可 rebuild 并重新通过 smoke。
- 如果 package 路径为保持渲染可用绕过了某些 resource graph 字段，必须记录并挂到 `REQ-071-f` 的清理事项。

## 修改范围

- `src/core/scene/` / `src/core/resource/`：resource graph serialization model。
- `src/infra/scene_io/`：`.lxpkg` header、section table、chunk table、streaming restore read/write。
- `src/infra/offline/` 或 `src/tools/`：package build CLI/entry。
- `src/demos/lxe_editor/`：package load/rebuild UI 和 progress。
- `src/backend/vulkan/`：cache blob metadata 接入。
- `src/test/`：package round trip、resolver、hash、cache fallback 测试。

## 边界与约束

- 本 REQ 不实现复杂压缩格式。
- 本 REQ 不做 mesh by material 合批。
- 本 REQ 不把 package cache 当作源文件替代品；source scene 和资源 URI 仍保留。
- 本 REQ 不考虑 scene package 跨平台可读性；package 面向本机、本 engine build 和兼容 backend 快速恢复，不匹配就 rebuild。
- 本 REQ 不实现跨 GPU vendor pipeline cache 兼容；不匹配就重建。
- 本 REQ 要求 package 快速加载路径是单一二进制容器；YAML/JSON + 资源目录只能作为调试导出，不是验收路径。

## 依赖

- `REQ-071-c`：CPU resource graph 和 URI identity。
- `REQ-071-d`：GPUResourceTable cache metadata 和 upload tasks。
- `REQ-070-a`：BMW M6 转换输出可作为 package 输入。

## 后续工作

- `REQ-071-f`：用 package 加载 helmet/BMW，并做 offline/realtime 对齐验收。
- 后续独立 REQ 可评估 mesh 合批、section compression、mmap、增量 package patch。
- 清理本 REQ 为保持 helmet package smoke 可用而引入的 package load bridge。

## 实施状态

未实施。本文档用于确认 scene package 快速加载和 MaterialTemplate grouping。
