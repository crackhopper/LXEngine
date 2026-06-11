# 概念与设计：先建立地图，再进入子系统

概念与设计章节像一张工程地图。它不替代源码，也不替代当前设计 spec；它负责先告诉我们“这座引擎城市有哪些区、主路怎么连、哪些路还在施工”，然后把读者引到资产、材质、场景、Editor、源码分析和 roadmap。

## 这一章解决什么问题

初学者进入 LXEngine 时，最容易同时看到三类信息：代码目录、运行时对象、未来 roadmap。它们混在一起时会很难判断一个说法到底是当前代码、设计解释，还是未来目标。

本章统一用三种标记方式组织：

| 类型 | 含义 | 例子 |
|---|---|---|
| 当前实现 | 当前代码已经存在，可以按源码验证 | `SceneNode` component、`CommandBus`、Shadow/CSM FrameGraph |
| 设计解释 | 对当前实现的概念化说明 | 为什么 `core / infra / backend` 分层 |
| Roadmap / 未实施 | 未来方向，必须标注对应 requirement | Web Editor、engine-level MCP、AssetRegistry、HDR/Post、G-Buffer |

## 阅读顺序

1. [架构总览](architecture.md)：先看三层结构，以及 FrameGraph / shadow / CSM 到 GPU submit 的主链路。
2. [项目目录结构](project-layout.md)：再看文件放在哪里，哪些目录是事实来源。
3. [BuildInfo](build-info.md)：理解二进制版本标签如何进入 CLI、API、录制和离线输出 metadata。
4. [术语表](glossary.md)：遇到项目自造词时回查。
5. [资产系统](../concepts/assets/index.md)：理解文件、URI、序列化和 loader 边界。
6. [场景系统](../scene-system/index.md)：理解 scene、node、component、camera、light、renderable。
7. [材质系统](../concepts/material/index.md)：理解 material pass、pipeline identity 和未来 bindless 方向。
8. [引擎循环](../concepts/engine-loop.md)：理解一帧如何驱动 window、scene、renderer 和 GUI。
9. [渲染管线](rendering-pipeline/index.md)：在前面概念齐备后，再按 FrameGraph、RenderWork、RenderTarget、Shadow pass、CSM 的顺序拆开当前多 pass 数据流。
10. [编辑器系统](../design/editor-system/index.md)：理解 `lxe_editor` 如何把 UI、CommandBus、SceneRuntime、API 串起来。

## 当前章节结构

| 目录 | 放什么 | 不放什么 |
|---|---|---|
| `notes/concepts-design/` | 总览、目录结构、术语等跨系统地图 | 单个子系统的长篇细节 |
| `notes/concepts/` | 资产、材质、引擎循环等概念页 | 源码逐行分析 |
| `notes/scene-system/` | 场景系统的节点、组件、相机、光源、可渲染对象 | 资产文件格式细节 |
| `notes/design/` | Editor 等跨模块设计说明 | 临时草稿 |
| `notes/source_analysis/` | 贴源码的实现解析 | 面向新人建立概念的第一入口 |
| `notes/requirements/` | 未实施或正在实施的需求 | 已经落地的长期设计解释 |

## 继续阅读

- [架构总览](architecture.md)
- [项目目录结构](project-layout.md)
- [BuildInfo](build-info.md)
- [Roadmap](../roadmaps/README.md)
