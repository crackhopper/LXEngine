# REQ-075-d: Editor Offline Render Integration

> 原 `REQ-058-a` 重排到 `REQ-075-d`。当前代码已有
> `lxe_offline_render`、`outputProfiles`、RenderPathGraph-backed offline
> renderer、EXR/PNG 输出和实时/离线对比辅助工具；本 REQ 只保留 editor
> 侧离线调用、状态、取消和结果查看工作流。

## 背景

离线 renderer 的 CLI、output profile/render path graph 和输出文件能力已经存在，便于测试、CI、批处理和 ground truth 对比。editor 应提供入口来触发离线渲染、显示进度、查看 preview、打开输出目录，并最终接入 `REQ-075-b` 的实时/离线等价性对比结果。

场景资产仍由 editor 负责组织。对于 assets-downloader 生成的 cache 资产，本 REQ 只依赖当前 scene document / cache URI 能力，不要求完整 Asset Browser。

本 REQ 排在 `REQ-075-c` 之后。它不改变离线算法，只把稳定的离线路径接入 editor。

## 目标

1. editor 可启动 offline render job。
2. editor 显示 job 状态、进度和错误。
3. editor 可查看 PNG preview，打开 EXR/输出目录。
4. editor 不复制 CLI 逻辑，只构造共享 offline render request 并调用 CLI/subprocess 层。
5. 首版通过 `lxe_offline_render` 子进程执行离线渲染，避免把 headless Vulkan/offline renderer 生命周期直接嵌入 editor 进程。

## 需求

### R1: Shared Offline Render Request API

CLI 与 editor 使用同一个 offline render request API。首版 API 负责生成 request config、启动子进程、
收集状态和日志；后续再考虑 in-process offline renderer。

要求：

- request 输入来自 scene path、output profile、render path graph 和 command override。
- job 输出状态包含 queued/running/succeeded/failed/cancelled。
- job 错误可序列化给 editor API。
- request config 可被 CLI、editor、CI 复用。

### R1.1: 子进程执行策略

首版 editor 执行 offline render 时启动 `lxe_offline_render` 子进程。

要求：

- editor 将当前 scene 保存或导出到明确 scene path，再启动子进程。
- 子进程参数来自 offline render request，包括 scene、output profile、尺寸/output override 和 output path。
- editor 捕获 stdout/stderr，写入 job log。
- editor 通过进程退出码和输出文件状态判断 success/failure。
- cancel 操作终止子进程，并把 job 标记为 `cancelled`。
- editor 进程不直接持有 `VulkanOfflineRenderer` 或 headless Vulkan device。

后续当 Vulkan 生命周期、资源隔离和 job queue 稳定后，可以增加 in-process backend，但不能破坏首版 CLI/subprocess 行为。

### R2: Editor command

新增 command bus 命令，例如：

```text
offline render --profile preview
offline render --profile reference --samples 64
offline open-output
```

要求：

- 命令能在当前 active scene 上运行。
- 支持指定输出目录。
- 失败时不会污染 scene dirty 状态。

### R3: UI 状态

ImGui editor 首版最小 UI：

- profile 选择
- start/cancel
- progress/status
- last output path
- preview PNG
- open output directory

### R4: Realtime/offline 对比预留

首版可以只显示 preview。若 `REQ-075-b` 或 CLI 工具已经产出比较结果，editor 首版只需要保存足够 metadata，供后续面板显示：

- 当前 viewport screenshot
- offline PNG
- difference heatmap
- EXR 数值对比

### R5: 测试覆盖

覆盖：

- command 能启动 fake/offline job。
- job 状态事件能被 API 查询。
- 失败 job 返回明确错误。
- editor reload scene 后不会丢失已完成输出路径。

## 修改范围

- `src/demos/lxe_editor/`
- CommandBus
- editor API service
- offline renderer shared module
- ImGui UI
- tests / use cases

## 边界与约束

- 本 REQ 不要求 Web Editor。
- 本 REQ 不要求实时 progressive viewport path tracing。
- 本 REQ 首版不在 editor 进程内执行 offline renderer；使用 `lxe_offline_render` 子进程。
- 本 REQ 不实现新的 renderer 算法。

## 依赖

- `REQ-074-h`
- `REQ-074-i`
- `REQ-075-b`
- `REQ-075-c`（仅当 editor 需要 reference/path tracing profile）

planned bake job 不属于本 REQ 依赖；未来若启动 bake 实现，再扩展 editor 入口。

## 后续工作

- Web Editor offline render panel。
- realtime/offline difference view。
- 完整 Asset Browser / AssetRegistry / hot reload。
- render farm / batch queue。

## 实施状态

2026-06-14 重排后状态：保留 active，后置到 `REQ-075-d`。核心 offline job/CLI 已存在，但 editor 集成尚未完成。

当前已有 `lxe_offline_render` CLI、output profile/render path graph 解析、FrameGraphExecutor-backed offline renderer 和 offline image writer；editor 侧还没有完整 offline job 面板、命令/API、进度、取消、输出预览和结果定位工作流。
