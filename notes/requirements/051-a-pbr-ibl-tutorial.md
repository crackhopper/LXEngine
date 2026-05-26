# REQ-051-a: PBR IBL Tutorial

> 2026-05-26 新增：本 REQ 在底层能力和测试场景完成后，写一篇从 0 到 1 手动搭建 PBR + IBL 金属球场景的教程。

## 背景

PBR + IBL 涉及资源、FrameGraph、材质、shader、scene 和 post-process 多条路径。如果只留下测试场景，后续开发者很难理解“为什么必须先有 HDR/Post，再有 IBL bake，再由 PBR material 消费”。本教程要把完整路径讲成可跟随的操作和机制说明。

教程必须描述已实现的当前事实，不把未来 local reflection probe 当成已经存在的能力。

## 目标

1. 解释 HDR/Post/PBR/IBL 的最小闭环。
2. 指导读者手动创建或理解 metal sphere scene。
3. 说明每个资源和 shader binding 的归属。
4. 给出验证和排错路径。
5. 明确 local reflection probe bake 是后续扩展。

## 需求

### R1: 教程位置与导航

新增教程文档。

建议路径：

- `notes/tutorial/pbr-ibl-metal-sphere.md`

要求：

- 加入 `notes/nav.yml`。
- 遵守 `notes-writing-style`：使用“我们”视角、结构化表格、当前事实叙述。
- 不复述过时实现或兼容路径。

### R2: 从资源开始讲清闭环

教程按实际依赖顺序组织。

建议章节：

1. HDR environment 作为输入。
2. GPU bake 生成 skybox / irradiance / prefiltered env / BRDF LUT。
3. Forward 写 HDR scene color。
4. PostProcess 做 exposure、tone mapping、gamma 和 bloom。
5. PBR material 采样 IBL resources。
6. metal sphere scene 的节点和参数。

### R3: YAML / material / shader 对照

教程必须包含配置到运行时对象的对应关系。

要求：

- 展示 scene YAML 中 environment / material / mesh 的关键字段。
- 展示 PBR `.material` 中参数和 texture binding。
- 用表格解释 material-owned binding 与 scene-level IBL binding 的边界。
- 解释为什么 `IrradianceMap` / `PrefilteredEnvMap` / `BrdfLut` 不写在 `.material resources` 中。

### R4: 操作步骤

教程提供可执行步骤。

要求：

- 如何构建 shader。
- 如何启动 `lxe_editor`。
- 如何加载 metal sphere scene。
- 如何截图或 dump post output。
- 如何判断看到的是 IBL，而不是固定 ambient。

### R5: 排错章节

教程包含常见问题。

至少覆盖：

- 金属球发黑：IBL resources 未注入或 bake 失败。
- 画面过曝/过暗：exposure/tone mapping 参数。
- 反射方向不对：cubemap face orientation。
- 没有 bloom：threshold/intensity 或 post stack 配置。
- Vulkan/headless 环境无法截图：使用 `xvfb-run` 或按测试 skip 信息排查。

### R6: 测试覆盖

至少覆盖：

- docs link check 或 notes build 能通过。
- 教程中引用的文件路径存在。
- 教程中的 command 与当前构建/运行方式一致。
- 教程不声称 local reflection probe 已实现。

## 修改范围

- `notes/tutorial/`
- `notes/nav.yml`
- `notes/concepts/material/`（如需增加短链接或入口说明）
- `notes/concepts-design/rendering-pipeline/`（如需增加 HDR/Post 当前事实链接）

## 边界与约束

- 本 REQ 不实现新渲染功能。
- 本 REQ 不写 API reference 风格文档。
- 本 REQ 不描述未实现 probe bake 为当前能力。
- 本 REQ 不要求录制长视频或制作外部媒体资产。

## 依赖

- `REQ-046-a`
- `REQ-047-a`
- `REQ-048-a`
- `REQ-049-a`
- `REQ-050-a`
- `openspec/specs/notes-writing-style/spec.md`

## 后续工作

- local reflection probe bake 完成后，新增进阶教程或在本教程末尾增加“下一步”链接。

## 实施状态

已完成。

已落地：

- 新增 `notes/tutorial/pbr-ibl/` 教程组，覆盖 metal sphere scene、资源/binding 边界、HDR Post 流程、验证与排错。
- `notes/nav.yml` 和 `notes/tutorial/index.md` 已加入 PBR + IBL 教程入口。
- 教程已更新为当前实现事实：SceneRuntime 保留 CPU preview/fallback 与 `EquirectangularMap` 输入，VulkanRenderer 在 `initScene()` 阶段执行 GPU IBL bake，Forward HDR skybox 背景与 PBR material 均优先消费 baked scene-level IBL resources。
- `scripts/notes/serve_site.sh --build` 已通过；输出中仍有仓库既有历史链接 warning，但本教程与导航能构建进站点。
