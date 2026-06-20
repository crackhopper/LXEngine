# Use Case: 验证 PBR IBL Helmet 场景

这个 use case 用于让 Codex 通过 `lxe_manager` MCP 驱动远端
`lxe_editor`，验证
`assets/scenes/generated/helmet_standard_pbr.scene.yaml` 能展示当前
PBR + IBL + Forward HDR/Bloom 链路。它面向实现验收，不是逐像素截图基准。

## 目标

- 打开当前 Helmet standard PBR scene asset，确认 scene/runtime 状态稳定。
- dump `hdr.color`，确认 Forward HDR attachment 可诊断。
- 目检或截图确认 Damaged Helmet、直射光和 IBL 结果可见。

## 前置条件

- Codex 已连接远端 `lxe_manager` MCP。
- 远端 editor 的 build identity 应与当前 Git HEAD 匹配；如不匹配，先使用
  `lxe-verify-implement` 工作流拉取、构建并重启 editor。
- Vulkan/video device 可用；如果远端只能 headless 运行，应至少执行 dump
  验证并记录无法截图的原因。
- `assets/env/khronos/neutral/ggx/specular.ktx2` 和 Damaged Helmet 资产存在。

## 场景和坐标

- 场景：`assets/scenes/generated/helmet_standard_pbr.scene.yaml`
- 关键节点：
  - `/game_cam`
  - `/neutral_infinite_skybox`
  - `/finite_neutral_room`
  - `/compare_key_light`
  - `/damaged_helmet`
- 推荐 dump 路径：
  `data/debug/dump/helmet-standard-pbr-hdr.bmp`

## 业务步骤

1. 查询 editor 状态和 build identity。
2. 执行 `scene open assets/scenes/generated/helmet_standard_pbr.scene.yaml`。
3. 等待 `state summary` 中 `sceneName` 为 `helmet_standard_pbr` 或显示当前 Helmet Standard PBR scene 已加载。
4. 执行 `state cameras`，确认 active gameplay camera 是 `/game_cam`。
5. 执行 `preview on`，让 viewport 使用 gameplay camera。
6. 执行 `render debug dump hdr.color data/debug/dump/helmet-neutral-ibl-hdr.bmp`。
7. 读取命令结果，确认 dump 成功，格式为 HDR attachment 格式
   `R16G16B16A16_SFLOAT` 或等价后端名称，输出宽高非零。
8. 如 MCP display 可用，读取当前显示截图；否则使用用户侧 editor 画面目检。
9. 可选：执行 `select /damaged_helmet`，确认 scene path 可解析并便于后续 pick/目检。

## 验收标准

- `state summary` 显示 runtime scene 已加载完成，而不只是 project 文档切换。
- `hdr.color` dump 文件存在且非空。
- 画面中能看到 Damaged Helmet。
- Helmet 不是纯黑、纯白或无光照 flat color。

## 失败排查

- `render debug dump unavailable`：确认 editor 初始化时传入了 Vulkan renderer
  dump hook，且当前运行的不是无渲染后端。
- `hdr.color` dump 失败：先执行 `state summary`，确认 scene 已完成加载；
  再确认当前 renderer 已至少绘制过一帧。
- 画面纯黑：确认 `neutral_environment` 的 `environment.feature.uri` 可读，
  `neutral_infinite_skybox` 的 `skybox.mode` 为 `infinite`，
  `damaged_helmet` 的 `bake.ibl.enabled` 为 true，且 render resource parser 测试通过。
- Helmet 没有 IBL 观感：运行 `test_render_resource_parsers` 和
  `test_shader_compiler`，确认 graph/material/shader 合同仍然接通，再 dump
  renderer 目标检查 IBL 资源是否实际上传。
