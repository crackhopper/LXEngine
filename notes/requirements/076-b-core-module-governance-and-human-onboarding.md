# REQ-076-b: Core Module Governance And Human Onboarding

> 2026-06-16 新增：`REQ-076` 阶段用于代码治理、架构调整、文件拆分和核心模块边界管理。本 REQ 与 `REQ-076-a` 的大文件拆分互补：`076-a` 处理具体大文件，`076-b` 建立人类可以接入和长期维护核心模块的规则、文档和 review gate。

## 背景

`REQ-073` 到 `REQ-075` 会继续强化 renderer、OfflineRT、IBL 和 package / cache 路径。功能推进之后，核心模块如果缺少边界说明、owner map、review checklist 和稳定源码阅读入口，人类协作者很难安全接入，也容易在后续 `REQ-077` 到 `REQ-080` 中重新引入临时桥、重复系统或大文件堆积。

本 REQ 把治理从功能需求中拆出来，集中处理“如何让核心模块可读、可审、可长期维护”。

## 目标

1. 建立核心模块 owner map 和职责边界。
2. 为 renderer、SceneResourceTable、RenderPathGraph、Material、OfflineRT、editor runtime 等核心模块建立 review checklist。
3. 把当前 notes / source analysis / subsystem docs 对齐到真实代码入口。
4. 明确哪些路径禁止新增兼容桥、placeholder resource 或手写 fallback。
5. 给人类接入提供最短阅读路径和常用验证命令。

## 非目标

- 不实现新的渲染功能。
- 不替代 `REQ-076-a` 的具体大文件拆分。
- 不重写所有文档；只修正核心路径和高频接入文档。
- 不改变 active requirement 的功能优先级。

## 需求

### R1: Core Module Owner Map

SHALL 建立核心模块清单，至少覆盖：

- Material / shader contract。
- SceneResourceTable / upload view。
- RenderPathGraph / FrameGraph / RenderWorkCompiler。
- Vulkan realtime backend。
- OfflineRT backend。
- editor scene runtime。
- notes / requirement workflow。

每个模块 SHALL 说明 owner 文件、主要数据结构、允许依赖和禁止依赖。

### R2: Review Gate

SHALL 为核心模块建立 review checklist：

- 是否新增第二套 public contract。
- 是否接受但忽略 parser 字段。
- 是否用 placeholder payload 满足真实依赖。
- 是否绕过 RenderFeature / material source / SceneResourceTable 的事实源。
- 是否新增大文件职责。

### R3: Human Onboarding Path

SHALL 提供人类接入阅读路径：

- 先读哪些 notes。
- 再读哪些 source analysis。
- 最后看哪些代码入口。
- 修改后跑哪些最小验证命令。

## 测试

- docs build 通过。
- 核心模块入口链接有效。
- review checklist 能映射到至少三个近期 active REQ 的验收项。

## 修改范围

- `notes/concepts-design/`
- `notes/subsystems/`
- `notes/source_analysis/`
- `AGENTS.md` / agent-facing index，如确有必要。

## 实施状态

未实施。
