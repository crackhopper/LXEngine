# Use Case: 录制一次复杂场景编辑

这个 use case 用于让 Codex 通过 `lxe_manager` MCP 驱动远端
`lxe_editor`，完成一次接近真实使用的场景操作，并保存可回放的录制文件。
它不是逐帧 UI 自动化脚本，而是面向 agent 的业务流程说明。

## 目标

- 从 `empty` project template 初始化一个临时项目，并打开它的 `main` 场景。
- 调整 `editor_cam` 的观察角度，避免默认视角只命中 `game_cam`。
- 新增一个可编辑 primitive，执行若干编辑命令，并保存回项目当前场景。
- 保存录制文件，读取录制内容，并至少回放一次。

## 前置条件

- Codex 已连接远端 `lxe_manager` MCP。
- `ops.editor_status` 显示 editor 正在运行；如果没有运行，先用
  `ops.editor_start` 或远端修复工作流启动。
- 远端 editor 的 build identity 应与当前 Git HEAD 匹配；如不匹配，使用
  `lxe-remote-fix-rebuild-retest` 工作流拉取、构建并重启 editor。
- recording 默认可以是关闭状态，本流程会显式开启。

## 场景和坐标

- 项目模板：`empty`
- 项目名：`codex_recording_use_case_<timestamp>`，每次执行使用新的后缀，避免
  和远端已有项目目录冲突。
- 场景：`scenes/main.scene.yaml`
- 推荐 viewport/pick 坐标：先使用 `pick screen 640 360 1280 720`。
- 如果中心点未命中业务节点，先读取 `state cameras` 和 `state selection`，
  再调整 `cam look-at` 的 eye/target 或换一个 pick 坐标。

## 业务步骤

1. 查询 editor 状态和 build identity。
2. 选定本次唯一项目名，例如 `codex_recording_use_case_20260515_001`。
3. 执行 `recording enable` 和 `recording start basic`。这一步要放在
   `project init` 之前，让录制 steps 包含项目创建和场景打开过程。
4. 执行 `project init empty <project-name>`。
5. 执行 `scene new recording_scratch`，等待 `state summary` 中
   `project.activeScene` 为 `scenes/recording_scratch.scene.yaml`，且
   `sceneName` 为 `recording_scratch`。
6. 执行 `scene open main`。
7. 等待 `state summary` 中 `project.id` 为 `<project-name>`，
   `project.activeScene` 为 `scenes/main.scene.yaml`，且 `sceneName` 为
   `Empty Project`。这里必须同时确认 `sceneName`，因为 `project.activeScene`
   只说明项目文档已经切换，不能单独证明 runtime scene 已经绑定完成。
8. 执行 `preview off`，确保 pick 使用 editor camera。
9. 执行 `deselect`，避免 `cam look-at` 优先操作当前选中的 camera 节点。
10. 执行 `cam control orbit`，让 toolbar 状态处于标准编辑控制模式。
11. 执行 `cam look-at 2.8 2.0 4.5 0.0 0.6 0.0`，把 editor camera 从默认
   game camera 视角旋开。
12. 执行 `state cameras`，确认 `editor.eye` 已经变化。
13. 执行 `add primitive:cube recording_cube`。
14. 执行 `select /recording_cube`，确认后续编辑目标存在。
15. 执行 `pick screen 640 360 1280 720`。
16. 执行 `state selection`。如果仍选中 `/game_cam` 或没有选中，执行
    `pick screen 520 360 1280 720` 或 `pick screen 760 360 1280 720` 后再确认。
17. 执行 `select /recording_cube`，把目标重新固定到新增 primitive。
18. 执行 `move /recording_cube 0.25 0.1 0.0`。
19. 执行 `rotate /recording_cube 0 25 0`。
20. 执行 `scale /recording_cube 1.05`。
21. 执行 `preview on`，再执行 `preview off`，验证编辑 / gameplay camera 切换。
22. 执行 `scene save`。
23. 执行 `recording stop save`。
24. 用 `recording_list` 找到最新录制 id。
25. 用 `recording_read` 读取最新录制，确认 steps 中包含 `project init`、
    `scene new`、`scene open main`、camera、pick、select、move、rotate、
    scale、preview、scene save 等操作。因为录制从项目创建前开始，
    metadata.scenePath 可以为空；回放时以 steps 中的 project / scene 命令
    建立上下文。
26. 用 `recording_replay` 回放最新录制。
27. 用 `recording_probe` 读取 `summary`、`project`、`selection`、`cameras`。

## 验收标准

- `state summary` 显示项目已打开，`project.activeScene` 为
  `scenes/main.scene.yaml`。
- `state cameras` 显示 active camera 在 preview off 时是 editor camera。
- 录制文件保存成功，且 step 数大于 8。
- 录制 JSON 的 steps 中能看到 project init、scene new、scene open、camera
  pose 调整、pick、编辑命令和保存命令。
- `recording_replay` 返回成功；如失败，需要记录失败 step id 和 probe 结果。
- `data/projects/<project-name>/scenes/main.scene.yaml` 在远端更新；如果
  project catalog 对重名目录加了后缀，以 `project status` 返回的 path 为准。

## 失败排查

- `editor_unavailable`：切换到 `lxe-manager-ops` 查询 `ops.editor_status`。
- pick 命中 `/game_cam`：说明 editor camera 角度或 pick 坐标不合适，先执行
  `cam look-at` 再重新 pick。
- `recording_cube` 路径不存在：重新执行 `add primitive:cube recording_cube`，
  再执行 `select /recording_cube`。
- `recording_replay` 失败：读取失败 step，使用 `recording_probe` 采集
  `summary`、`selection`、`cameras`，再对照录制 JSON 判断是命令不可重放还是
  当前 scene 状态不一致。
