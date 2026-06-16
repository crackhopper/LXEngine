# REQ-076-a: Large File Decomposition Backlog

> 2026-06-16 校准：`REQ-076` 阶段预留给代码治理、架构调整、文件拆分和核心模块边界管理。旧 `REQ-069-a/b/c` 按“一份大文件一个需求”拆开管理，实施顺序过早且容易和功能主线交织；当前先合并为一个治理需求，后续执行时一次只拆一个大文件，保持等价搬迁和测试闭环。

## 背景

当前代码中仍有三块明确的大文件拆分债务：

| 文件 | 当前行数 | 当前状态 | 主要风险 |
|---|---:|---|---|
| `src/backend/vulkan/vulkan_realtime_renderer.cpp` | 3364 | 已有 `VulkanRendererFoundation`、`VulkanPostProcessBuilder`、`IblBakeRenderer` 等局部 helper，但 FrameGraph 执行、dump/profile、post/skybox/shadow 仍集中在主文件 | renderer hard cut 和 diagnostics 容易被主文件状态掩盖 |
| `src/core/editor/commands/builtin_commands.cpp` | 3583 | core command 仍集中注册和实现；demo/editor command 已有一批独立文件，但 core builtin command domain 未拆 | 新命令继续堆入同一文件，help/completion/JSON helper 难复用 |
| `src/demos/lxe_editor/scene_runtime.cpp` | 2347 | scene document load/save、runtime scene build、asset discovery、material surface、procedural state、camera/light capture 仍集中 | SceneRuntime 容易复制 scene_io/material/component 职责 |

这些拆分都属于代码结构和可维护性治理。它们不应该打断 `REQ-073` 到 `REQ-075` 的渲染功能闭环，但应在 `REQ-077` 3DGS 和后续复杂 render path 继续扩张前形成一轮可维护性收束。因此本 REQ 把旧 `REQ-069-a/b/c` 汇总为治理 backlog，并给出执行顺序、边界和验收。

## 目标

1. 用一个 active REQ 管理当前三处大文件拆分，不再让 `069-a/b/c` 分散占据实施队列。
2. 拆分时只做等价搬迁、helper 去重和测试补强，不夹带新渲染功能或 editor 行为。
3. 每次只拆一个文件，完成构建、测试和 source analysis 后再进入下一个文件。
4. 用当前代码事实决定拆分边界：优先复用已有 foundation、scene_io、material、command bus 和测试目标。

## 非目标

- 不新增 renderer feature、editor command、scene schema 或 material 行为。
- 不替代 `REQ-073-*` / `REQ-074-*` 的 hard cut，也不替代 `REQ-076-c` 的非 mesh 架构扩展。
- 不在拆分时实现 3DGS、package、offline/realtime equivalence 或 PBRT 材质能力。
- 不用“降行数”作为唯一目标；每个新文件必须有清晰职责和测试覆盖。

## 实施顺序

| 顺序 | 子任务 | 原需求来源 | 进入条件 |
|---|---|---|---|
| S1 | Vulkan realtime renderer 拆分 | `REQ-069-a` | `REQ-073-*` 的 renderer/material hard cut 边界清楚，当前材质/FrameGraph diagnostics 不被拆分削弱 |
| S2 | Core editor builtin commands 拆分 | `REQ-069-b` | command bus 回归测试可跑，命令域清单完成 |
| S3 | LXE editor SceneRuntime 拆分 | `REQ-069-c` | scene runtime 回归测试可跑，scene_io/material/component 复用边界完成 |

执行时 MAY 调整 S1/S2/S3 的具体先后，但同一轮实现不应同时重写两个大文件。

## 需求

### R1: 职责审计先行

每个子任务开始前 SHALL 先列出：

- 当前文件中的职责块。
- 已有可复用模块。
- 新 helper 的单一职责。
- 不拆出的原因。
- 对应测试目标。

审计结果 SHALL 写入 implementation note、source analysis 或本 REQ 的实施状态更新中。

### R2: Vulkan Realtime Renderer 拆分

`src/backend/vulkan/vulkan_realtime_renderer.cpp` SHALL 保留 realtime 总控职责，把执行细节迁到 helper。

优先拆分模块：

| 模块 | 职责 |
|---|---|
| `vulkan_realtime_frame_graph_executor.*` | `drawPassQueue()`、`submitBindlessQueue()`、offscreen pass prepare、traditional/dynamic pass record、attachment transition |
| `vulkan_render_target_dumper.*` | frame graph attachment dump、debug render target dump、stats/readback、screen dump |
| `vulkan_fullscreen_item_builder.*` | bloom threshold/blur、deferred lighting、skybox background、standard post item |
| `vulkan_shadow_runtime.*` | directional cascade update、shadow UBO snapshot、shadow descriptor binding |
| `vulkan_realtime_profile_output.*` | profile output metadata、linear output、CPU sRGB output |
| `vulkan_scene_resource_binder.*` | IBL bake injection、FrameGraph sampled resource attachment、scene-level descriptor preparation |

约束：

- 继续复用 `VulkanRendererFoundation`、`VulkanResourceManager`、`VulkanPostProcessBuilder` 和 `IblBakeRenderer`。
- 新 helper 使用 constructor injection，不持有整个 realtime renderer `Impl`。
- 不新增 material/URI/offline fallback；遇到 legacy path 时保留现有 fail-fast diagnostics。
- `VulkanRenderer` facade 仍只转发，不能重新变成实现类。

### R3: Core Editor Builtin Commands 拆分

`src/core/editor/commands/builtin_commands.cpp` SHALL 按 command domain 拆分。

推荐结构：

```text
src/core/editor/commands/
  register_builtin_commands.cpp
  command_parse_helpers.hpp/.cpp
  command_json_helpers.hpp/.cpp
  scene_commands.cpp
  node_commands.cpp
  camera_commands.cpp
  light_commands.cpp
  material_commands.cpp
  selection_commands.cpp
  debug_probe_commands.cpp
  undo_redo_commands.cpp
```

约束：

- `builtin_commands.hpp` 对外入口保持稳定。
- 不复制 parser、JSON formatter、scene lookup 或 material helper；共享逻辑集中到 helper 文件。
- dispatch、help、completion、structured JSON 输出保持等价。
- `test_lxe_editor_source_boundary` 继续覆盖 editor 与 core/source boundary；
  命令行为回归需要在本 REQ 实施时补新的小型 focused test。

### R4: LXE Editor SceneRuntime 拆分

`src/demos/lxe_editor/scene_runtime.cpp` SHALL 拆成 editor-facing facade 和小 helper。

推荐结构：

```text
src/demos/lxe_editor/
  scene_runtime.cpp
  scene_runtime_assets.hpp/.cpp
  scene_runtime_document_capture.hpp/.cpp
  scene_runtime_materials.hpp/.cpp
  scene_runtime_procedural.hpp/.cpp
```

约束：

- `SceneRuntime` 保留 editor-facing facade：load/save、scene access、camera query、material query/update。
- document capture/restore 复用 `infra/scene_io` 类型，不扩展 YAML schema。
- asset discovery 不复制 project/session path 逻辑。
- material surface 复用 `MaterialInstance`、material loader 和 component API。
- 场景加载侧以 `test_gltf_scene_asset_loader` 和
  `test_render_resource_parsers` 作为当前保留回归；更细的 scene runtime
  focused tests 需要在本 REQ 实施时补回。

### R5: Source Analysis And Docs

每完成一个子任务，SHALL 更新对应 source analysis 或设计说明，至少记录：

| 字段 | 说明 |
|---|---|
| 新文件 | 文件名和单一职责 |
| 复用模块 | 依赖的已有模块 |
| 不负责 | 明确排除的职责 |
| 测试 | 对应 build/test target |

### R6: 验收命令

建议每个子任务至少运行：

```bash
cmake --build build --target \
  lxe_editor \
  test_bindless_validation_contract \
  test_bindless_indirect_contract \
  test_lxe_editor_source_boundary \
  test_gltf_scene_asset_loader \
  test_render_resource_parsers -j2

ctest --test-dir build --output-on-failure -R \
  "test_(bindless_validation_contract|bindless_indirect_contract|lxe_editor_source_boundary|gltf_scene_asset_loader|render_resource_parsers)"
```

如果运行环境没有 video device，按仓库约定对 Vulkan/windowed tests 使用 `xvfb-run -a`。

## 修改范围

- `src/backend/vulkan/vulkan_realtime_renderer.*`
- 新增 Vulkan realtime helper 文件
- `src/core/editor/commands/builtin_commands.*`
- 新增 core command domain/helper 文件
- `src/demos/lxe_editor/scene_runtime.*`
- 新增 scene runtime helper 文件
- 相关 tests
- 相关 source analysis / notes

## 边界与约束

- 每个提交只处理一个子任务或一个明确职责块。
- 不为了拆文件改 public API；必须改时先记录原因和调用点。
- 不把 fallback 或 compatibility bridge 藏进新 helper。
- 不把测试留到三个文件全部拆完之后再补。

## 依赖

- `REQ-074-h` / `REQ-074-i`: OfflineRT default graph path 与旧 side-channel 删除。
- `REQ-073-e` / `REQ-073-j`: realtime material batching and hard cut diagnostics。
- `REQ-076-c`: RenderPathGraph / material / effect 非 mesh 扩展完成后，再拆 renderer 主文件可避免把新边界藏进 helper。

历史上 `REQ-054-a`、`REQ-068-a`、`REQ-071-b`、`REQ-071-c` 提供了 renderer boundary、profile output、RenderPathGraph 与 SceneResourceTable 基础，但它们已归档，不再作为 active 依赖。

## 实施状态

2026-06-14 状态：后置 active，未实施。

已完成：

- 旧 `REQ-069-a/b/c` 已从 active 合并到本 REQ。
- 代码现状已确认三处目标文件仍是 2000+ 到 3000+ 行级大文件。

仍未完成：

- Vulkan realtime renderer helper 拆分。
- Core builtin command domain 拆分。
- SceneRuntime helper 拆分。
- 每个子任务对应的 source analysis 和回归测试补强。
