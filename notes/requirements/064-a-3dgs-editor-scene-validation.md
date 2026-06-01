# REQ-064-a: 3DGS Editor Scene Validation

> 2026-05-28 新增：把 3DGS PLY 渲染能力收口到 editor 可重复验收场景。

## 背景

3DGS 能力不能只停留在 loader 或单元测试。仓库需要一个固定 scene，能通过 editor 打开、观察、截图和回归验证。

## 目标

1. 使用 `assets/scenes/3dgs_train_sample.scene.yaml` 作为固定验收场景。
2. 在 editor 中显示 3DGS 节点基础信息。
3. 通过 lxe_editor 命令或测试脚本完成 smoke 验证。

## 需求

### R1: 场景可打开

`lxe_editor` SHALL 能加载 `3dgs_train_sample.scene.yaml`，并显示 splat cloud。

### R2: Inspector 信息

选中 3DGS 节点后，Inspector SHALL 显示：

- asset URI
- splat count
- bounds
- SH degree
- GPU memory estimate

### R3: 命令 / API 验收

Editor command 或 API SHALL 能返回当前 scene 中 3DGS 节点摘要，方便自动化验收。

建议返回字段：

| Field | Meaning |
|---|---|
| `path` | scene node path |
| `uri` | source PLY URI |
| `splatCount` | splat 数量 |
| `shDegree` | 当前资源 SH degree |
| `boundsMin` / `boundsMax` | local-space bounds |
| `gpuBytes` | GPU buffer 估算或实际占用 |

### R4: 视觉回归

验收 SHALL 至少覆盖：

- 打开场景截图非空。
- orbit / pan 后截图仍非空。
- resize 后截图仍非空。
- 关闭 / 重新打开 scene 后资源能释放并重建。

### R5: 性能记录

验收 SHALL 记录样例 scene 的基础性能数据：

- PLY load time。
- CPU sort time。
- GPU upload time。
- steady-state frame time。

这些数据不作为首版硬性 FPS 门槛，但 SHALL 作为后续 GPU sort / compressed format 优化的基线。

## 修改范围

- `src/demos/lxe_editor/`
- `src/core/editor/`
- `notes/use_cases/`（如需记录 lxe_manager 验收用例）

## 依赖

- `REQ-063-b`

## 实施状态

Draft，未实施。
