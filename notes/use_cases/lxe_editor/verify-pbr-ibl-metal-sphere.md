# Use Case: 验证 PBR IBL 金属球场景

这个 use case 用于让 Codex 通过 `lxe_manager` MCP 驱动远端
`lxe_editor`，验证 `pbr_ibl` project template 和
`assets/scenes/ibl_metal_sphere.scene.yaml` 能展示当前 PBR + IBL + HDR
PostProcess 链路。它面向实现验收，不是逐像素截图基准。

## 目标

- 从 `pbr_ibl` project template 初始化一个临时项目。
- 打开默认 IBL metal sphere 场景，确认 scene/runtime 状态稳定。
- dump `scene.hdrColor`，确认 Forward HDR attachment 可诊断。
- 目检或截图确认金属球、参考地面和环境背景预览可见。

## 前置条件

- Codex 已连接远端 `lxe_manager` MCP。
- 远端 editor 的 build identity 应与当前 Git HEAD 匹配；如不匹配，先使用
  `lxe-verify-implement` 工作流拉取、构建并重启 editor。
- Vulkan/video device 可用；如果远端只能 headless 运行，应至少执行 dump
  验证并记录无法截图的原因。
- `assets/env/studio_small_03_2k.hdr` 存在。

## 场景和坐标

- 项目模板：`pbr_ibl`
- 项目名：`codex_pbr_ibl_use_case_<timestamp>`
- 默认场景：`scenes/ibl_metal_sphere.scene.yaml`
- 关键节点：
  - `/game_cam`
  - `/dir_light`
  - `/ground_reference`
  - `/metal_sphere`
- 推荐 dump 路径：
  `data/debug/dump/ibl-metal-sphere-scene-hdr.bmp`

## 业务步骤

1. 查询 editor 状态和 build identity。
2. 选定本次唯一项目名，例如 `codex_pbr_ibl_use_case_20260526_001`。
3. 执行 `project init pbr_ibl <project-name>`。
4. 等待 `state summary` 中：
   - `project.id` 为 `<project-name>`
   - `project.activeScene` 为 `scenes/ibl_metal_sphere.scene.yaml`
   - `sceneName` 为 `IBL Metal Sphere`
5. 执行 `scene list`，确认场景 catalog 中包含 `ibl_metal_sphere`。
6. 执行 `state cameras`，确认 active gameplay camera 是 `/game_cam`。
7. 执行 `preview on`，让 viewport 使用 gameplay camera。
8. 执行 `render debug dump scene.hdrColor data/debug/dump/ibl-metal-sphere-scene-hdr.bmp`。
9. 读取命令结果，确认 dump 成功，格式为 HDR attachment 格式
   `R16G16B16A16_SFLOAT` 或等价后端名称，输出宽高非零。
10. 如 MCP display 可用，读取当前显示截图；否则使用用户侧 editor 画面目检。
11. 可选：执行 `select /metal_sphere`，确认 scene path 可解析并便于后续 pick/目检。

## 验收标准

- `project init pbr_ibl` 成功，active scene 是 IBL metal sphere 场景。
- `state summary` 显示 runtime scene 已加载完成，而不只是 project 文档切换。
- `scene.hdrColor` dump 文件存在且非空。
- 画面中能看到金属球和地面参考物。
- 背景不是纯黑，并且应显示来自 `SkyboxMap` 的方向性环境背景，而不是单色清屏预览。
- 金属球不是纯黑、纯白或无光照 flat color。

## 失败排查

- `unknown project template: pbr_ibl`：确认当前 build 包含最新
  `assets/project_templates/pbr_ibl/project_template.yaml`。
- `render debug dump unavailable`：确认 editor 初始化时传入了 Vulkan renderer
  dump hook，且当前运行的不是无渲染后端。
- `scene.hdrColor` dump 失败：先执行 `state summary`，确认 scene 已完成加载；
  再确认当前 renderer 已至少绘制过一帧。
- 背景纯黑：确认 scene document 中 `scene.environment.enabled: true`，且
  `intensity > 0`，HDR 路径可读。
- 金属球没有 IBL 观感：运行 `test_render_resource_parsers` 和
  `test_shader_compiler`，确认 graph/material/shader 合同仍然接通，再 dump
  renderer 目标检查 IBL 资源是否实际上传。
