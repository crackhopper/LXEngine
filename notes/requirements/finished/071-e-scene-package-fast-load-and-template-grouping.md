# REQ-071-e: Superseded Scene Package Fast Load Scope

> 2026-06-12 更新：本 REQ 原本同时定义 `.lxpkg` 文件格式、SceneResourceTable 序列化、backend pipeline cache metadata、加载 UI、Merkle hash、MaterialTemplate grouping 和性能验收。后续改为在 `REQ-074-c` 到 `REQ-074-f` 中分段实施，并在 `REQ-073-g` 做完成后的硬切清理。

## 当前结论

本 REQ 不再作为 active implementation entry。

原 `071-e` 中仍有价值的内容按以下方式接管：

| 原范围 | 新归属 | 说明 |
|---|---|---|
| 单文件二进制 package 容器、section/chunk 表 | `REQ-074-c` | 先定义可演进文件格式，不同时实现所有 restore 逻辑 |
| `SceneResourceTable` persisted state 序列化 / restore | `REQ-074-d` | CPU resource graph、typed arrays、dependency graph、texture payload 等由此负责 |
| SceneResourceTable root hash / subtree hash | `REQ-074-d` | 作为 source parse 与 package restore 等价性的 CPU 侧验收 |
| Vulkan pipeline cache blob / backend cache metadata | `REQ-074-e` | 与 CPU package 分离，但作为 package optional backend section |
| source scene vs package load time benchmark | `REQ-074-f` | 以 BMW M6 为主要性能验收对象 |
| package 前旧路径硬切 | `REQ-074-b` | 防止旧 material/render fallback 被写进 package canonical state |
| package 后旧路径硬切 | `REQ-073-g` | package + GPU cache 路径完成后，再清理剩余 bridge |
| editor loading UI / async package restore progress | 后续独立 REQ | 不阻塞 package 文件格式和性能闭环 |

## 保留背景

BMW M6 这类多 mesh、多 material、多 texture 场景需要快速加载路径。目标仍然是把解析后的 canonical resource state 和必要 payload 写入本机二进制 package，再从 package restore 场景，减少 YAML/material/mesh/texture metadata 反复解析，并利用 Vulkan pipeline cache 降低 shader/pipeline 重新编译成本。

## 后续执行顺序

```text
REQ-074-a  BC7 texture compression pipeline
REQ-074-b  Pre-package hard cut for clean architecture data
REQ-074-c  LxScenePackage file format
REQ-074-d  SceneResourceTable package serialization and restore
REQ-074-e  GPU pipeline cache package metadata and Vulkan restore
REQ-074-f  BMW M6 package load performance comparison
REQ-073-g  Post-package hard cut and cleanup
```

## 实施状态

2026-06-14 复核关闭：已拆分接管；不从本 REQ 继续实施。本文件移出 active，package/canonical state/GPU cache/性能验收以 `REQ-074-b` 到 `REQ-073-g` 为准。
