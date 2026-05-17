# REQ-044-c: Roadmap 支撑 — Editor AssetRegistry 与热重载桥接

> 2026-05-17：本需求已从 active 队列移入 pending。AssetRegistry / 热重载仍属于 Phase 3，但不进入 v0.1.1 active 队列。

## 背景

当前资产系统以 runtime root、相对 URI、`.scene.yaml`、`.material`、内置 `asset.yaml` manifest 和 loader 为主。Roadmap Phase 3 计划引入 GUID、`.meta` sidecar、AssetRegistry、资源 handle 和热重载，但这些能力尚未落地。

Editor 设计文档已经需要解释 project/scene persistence 与未来资产管线的边界。为了避免把 GUID/热重载写成当前事实，需要把 editor 侧最小桥接需求列为 active requirement。

## 目标

1. 定义 editor 可查询的 AssetRegistry 最小模型。
2. 保持当前 URI/path scene 文件可用。
3. 为未来 GUID 引用、`.meta` 和热重载建立过渡层。
4. 让 Web Editor / ImGui Editor 都能查询同一份资产目录。

## 需求

### R1: Asset identity 最小模型

定义资产条目：

| 字段 | 含义 |
|---|---|
| `logicalUri` | 当前 scene/material 使用的 `assets/...` 或 builtin URI |
| `absolutePath` | runtime root 下的真实路径 |
| `type` | mesh / texture / material / shader / scene / prefab |
| `displayName` | editor 展示名 |
| `guid` | 可选；存在 `.meta` 时读取 |
| `source` | builtin / project / runtime |

### R2: Registry scan

Registry 至少扫描：

- `assets/materials/`
- `assets/models/builtin/**/asset.yaml`
- project-local `assets/`
- project-local `scenes/`

### R3: SceneRuntime 使用 registry 查询

当前 `SceneRuntime` 可以继续保存 URI，但加载时应能通过 registry 查询资产条目，用于错误诊断和 editor 展示。

### R4: 热重载事件占位

定义 asset event：

| event | 含义 |
|---|---|
| `asset.added` | 新资产被发现 |
| `asset.changed` | 文件 mtime 或 manifest 内容变化 |
| `asset.removed` | 资产消失 |
| `asset.reload_failed` | reload 失败 |

首版可以只实现 scan diff，不要求立即替换 GPU resource。

### R5: API / Editor 查询入口

ImGui / Web / MCP 都应能查询：

- asset list by type。
- asset detail by URI/GUID。
- builtin model catalog。
- material preset list。

### R6: 测试覆盖

覆盖：

- 扫描 runtime assets 得到 material/model/scene entries。
- project-local asset 覆盖或扩展 runtime asset。
- 缺失资产时错误包含 logical URI 和搜索 root。
- 修改 manifest 后 registry diff 产生 `asset.changed`。

## 修改范围

- `src/core/utils/filesystem_tools.*`
- `src/demos/lxe_editor/builtin_asset_catalog.*`
- `src/demos/lxe_editor/project_session.*`
- `src/demos/lxe_editor/scene_runtime.*`
- editor API service / future Web Editor API
- 相关 tests

## 边界与约束

- 本 REQ 不要求全面 GUID 化 `Mesh` / `Texture` / `MaterialInstance`。
- 本 REQ 不要求 shader/texture/mesh 即时 GPU 热替换。
- 本 REQ 不改变现有 `.scene.yaml` 的 URI 表面。
- 本 REQ 不实现资产数据库 UI，只提供查询模型。

## 依赖

- 当前 runtime asset root
- 当前 project/session 模型
- 当前 builtin asset catalog

## 实施状态

Pending，未开始。当前仅作为 Phase 3 Asset Pipeline 与 editor 之间的后续桥接需求。
