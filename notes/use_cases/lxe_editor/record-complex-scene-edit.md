# Use Case: 录制一次复杂场景编辑

这个 use case 用于让 Codex 通过 `lxe_manager` MCP 驱动远端
`lxe_editor`，完成一次接近真实使用的场景操作，并保存可回放的录制文件。
它不是逐帧 UI 自动化脚本，而是面向 agent 的业务流程说明。

## 目标

- 加载标准编辑器测试场景。
- 调整 `editor_cam` 的观察角度，避免默认视角只命中 `game_cam`。
- 点选场景对象，执行若干编辑命令，并保存到本地 `data/scenes/` 副本。
- 保存录制文件，读取录制内容，并至少回放一次。

## 前置条件

- Codex 已连接远端 `lxe_manager` MCP。
- `ops.editor_status` 显示 editor 正在运行；如果没有运行，先用
  `ops.editor_start` 或远端修复工作流启动。
- 远端 editor 的 build identity 应与当前 Git HEAD 匹配；如不匹配，使用
  `lxe-remote-fix-rebuild-retest` 工作流拉取、构建并重启 editor。
- recording 默认可以是关闭状态，本流程会显式开启。

## 场景和坐标

- 场景：`assets/scenes/lxe_editor.scene.yaml`
- 推荐 viewport/pick 坐标：先使用 `pick screen 640 360 1280 720`。
- 如果中心点未命中业务节点，先读取 `state cameras` 和 `state selection`，
  再调整 `cam look-at` 的 eye/target 或换一个 pick 坐标。

## 业务步骤

1. 查询 editor 状态和 build identity。
2. 执行 `recording enable` 和 `recording start basic`。
3. 执行 `scene load assets/scenes/lxe_editor.scene.yaml`。
4. 等待 `state summary` 中 `sceneName` 为 `lxe_editor`，且 `documentPath`
   指向测试场景。
5. 执行 `preview off`，确保 pick 使用 editor camera。
6. 执行 `cam control orbit`，让 toolbar 状态处于标准编辑控制模式。
7. 执行 `cam look-at 2.8 2.0 4.5 0.0 0.6 0.0`，把 editor camera 从默认
   game camera 视角旋开。
8. 执行 `state cameras`，确认 `editor.eye` 已经变化。
9. 执行 `pick screen 640 360 1280 720`。
10. 执行 `state selection`。如果仍选中 `/game_cam` 或没有选中，执行
    `pick screen 520 360 1280 720` 或 `pick screen 760 360 1280 720` 后再确认。
11. 执行 `select /helmet`。如果该路径不存在，先用 `list nodes` 查找
    一个 mesh 节点，并选择第一个业务 mesh 节点。
12. 执行 `move /helmet 0.25 0.1 0.0`。如果使用了替代节点，把路径替换
    为实际选中的业务 mesh 节点路径。
13. 执行 `rotate /helmet 0 25 0`。
14. 执行 `scale /helmet 1.05`。
15. 执行 `preview on`，再执行 `preview off`，验证编辑 / gameplay camera 切换。
16. 执行 `scene save data/scenes/codex-recording-use-case.scene.yaml`。
17. 执行 `recording stop save`。
18. 用 `recording_list` 找到最新录制 id。
19. 用 `recording_read` 读取最新录制，确认其中包含 scene load、camera、
    pick、select、move、rotate、scale、preview、scene save 等操作。
20. 用 `recording_replay` 回放最新录制。
21. 用 `recording_probe` 读取 `summary`、`selection`、`cameras`。

## 验收标准

- `state summary` 显示场景已加载。
- `state cameras` 显示 active camera 在 preview off 时是 editor camera。
- 录制文件保存成功，且 step 数大于 8。
- 录制 JSON 中能看到 camera pose 调整、pick、编辑命令和保存命令。
- `recording_replay` 返回成功；如失败，需要记录失败 step id 和 probe 结果。
- `data/scenes/codex-recording-use-case.scene.yaml` 在远端生成。

## 失败排查

- `editor_unavailable`：切换到 `lxe-manager-ops` 查询 `ops.editor_status`。
- pick 命中 `/game_cam`：说明 editor camera 角度或 pick 坐标不合适，先执行
  `cam look-at` 再重新 pick。
- 业务节点路径不存在：执行 `list nodes`，选择第一个非 helper、非 camera 的
  mesh 节点并替换后续路径。
- `recording_replay` 失败：读取失败 step，使用 `recording_probe` 采集
  `summary`、`selection`、`cameras`，再对照录制 JSON 判断是命令不可重放还是
  当前 scene 状态不一致。
