# REQ-071-d: Superseded GPUResourceTable / Pipeline Cache / Upload Scope

> 2026-06-12 更新：本 REQ 原本同时覆盖 GPUResourceTable、bindless、indirect draw、pipeline cache、scene package cache metadata、upload tasks 和 scene load / technique switch loading UI。该范围过大，并且部分术语已经被 RenderPathGraph / RenderPath 取代。后续不再从本文件直接实施；范围已拆分到 `REQ-073-b` 和 `REQ-074-c` 到 `REQ-074-g`。

## 当前结论

本 REQ 不再作为 active implementation entry。

原 `071-d` 中仍有价值的内容按以下方式接管：

| 原范围 | 新归属 | 说明 |
|---|---|---|
| bindless scene data upload、global texture/material/object tables | `REQ-073-b` | 紧跟 `REQ-073-a`，验证 Material v3 type/signature 数据真实进入 bindless / indirect 渲染路径 |
| non-bindless / per-material descriptor fallback 禁止 | `REQ-073-b` | 先验证新路径可用，再在 073-b 结束时硬切旧路径并跑 smoke |
| indirect draw 覆盖 validation path | `REQ-073-b` | 作为新材质 GPU record 的验收，不与 package 格式混在一起 |
| pipeline cache find/getOrCreate 语义 | `REQ-074-e` | 作为 GPU pipeline cache package metadata / Vulkan pipeline cache restore 的一部分 |
| Vulkan pipeline cache blob import/export | `REQ-074-e` | 配合 `.lxpkg` backend cache section |
| scene load / RenderPath switch progress UI | 后续独立 REQ | 这是 realtime/editor 加载体验工作，不阻塞 package 最小闭环 |
| upload task / async job UI | 后续独立 REQ | 等 package restore 和 GPU cache restore 路径稳定后再做 |
| `technique` validation 术语 | RenderPath / RenderPathGraph | 后续文档使用 RenderPath，不再用 material-local technique 作为主概念 |

## 保留背景

071 主线仍然要求渲染数据从 CPU `SceneResourceTable` 进入 GPU 统一资源表，并且 validation 路径不能静默回退到旧 descriptor / per-item submission。这个目标没有取消，只是从本文件拆到更小的实施窗口。

## 后续执行顺序

```text
REQ-073-a  Material v3 PBRT metallic extension
REQ-073-b  Bindless/indirect material path hard cut
REQ-074-a  BC7 texture compression pipeline
REQ-074-b  Package canonical state readiness gate
REQ-074-c  LxScenePackage file format
REQ-074-d  SceneResourceTable package serialization and restore
REQ-074-e  GPU pipeline cache package metadata and Vulkan restore
REQ-074-f  BMW M6 package load performance comparison
REQ-074-g  Post-package hard cut and cleanup
REQ-075-a  Offline/realtime render equivalence on the new architecture
```

## 实施状态

已拆分接管；不从本 REQ 继续实施。
