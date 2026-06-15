# REQ-054-a: Vulkan Renderer Boundary Hard Cut

> 2026-06-14 归档：`REQ-054-b` 的 Vulkan compute offline MVP 已经落地，当前代码也已经有 `VulkanRendererFoundation`、`VulkanRealtimeRenderer`、headless foundation wrapper 和 `backend::offline::VulkanOfflineRenderer`。本 REQ 不再保留 active；OfflineRT 默认图路径和 realtime/offline renderer 边界并入 `REQ-076-c`，`offlineShader` / old config bridge 删除并入 `REQ-076-d`，realtime renderer 大文件拆分并入 `REQ-076-j`。

## 背景

旧版 `REQ-054-a` 的设计压力来自一个问题：不要把 offline renderer 塞回当时的 `VulkanRendererImpl` 大类。现在这一步已经部分完成：

| 当前事实 | 代码位置 | 说明 |
|---|---|---|
| public facade 已变薄 | `src/backend/vulkan/vulkan_renderer.*` | `VulkanRenderer` 只委托 `VulkanRealtimeRenderer`，不直接承载 offline 分支 |
| realtime 主体已独立命名 | `src/backend/vulkan/vulkan_realtime_renderer.*` | 仍实现 `gpu::Renderer`，负责 swapchain、present、GUI、FrameGraph 和 realtime output |
| shared foundation 已存在 | `src/backend/vulkan/vulkan_renderer_foundation.*` | 支持 `createRealtime()` / `createHeadless()`，创建 device、command manager、resource manager |
| headless foundation smoke 已存在 | `src/backend/vulkan/vulkan_offline_renderer.*` | 用 foundation 创建 headless Vulkan context，`test_vulkan_offline_renderer` 覆盖无 surface |
| offline render API 已独立 | `src/backend/vulkan/offline/vulkan_offline_renderer.*` | 通过 integrator 执行 `OfflineRenderJob`，不继承 realtime renderer |
| software compute integrator 已存在 | `src/backend/vulkan/offline/software_compute_offline_integrator.*` | 当前仍自己创建 headless device/command/resource manager |

同时，当前代码还保留多类过渡兼容路径，部分正在由 `REQ-073-*` 清理：

| 兼容路径 | 当前代码表现 | 清理归属 |
|---|---|---|
| OfflineRT 旧入口 | `OfflineRenderJob::offlineShader`、`createOfflineRenderFrameGraph()`、`Pass_OfflineRayTrace` 特判 | `REQ-076-c` / `REQ-076-d` |
| realtime material / descriptor fallback | `DescriptorResourceList` 等价、legacy material validation、旧材质 record 边界 | `REQ-073-e` / `REQ-076-b` |
| `techniques/...` shader URI | 默认资产和 positive path 的 URI/术语迁移 | `REQ-073-d` |
| package / pipeline cache restore | package canonical state、GPU cache blob、post-package cleanup | `REQ-074-*` |
| renderer public facade | `VulkanRenderer` 名称仍保留给 editor | 本 REQ 允许保留，但必须保持 thin facade |

因此，本历史 REQ 的目标是让 Vulkan renderer 结构本身干净：realtime/offline/foundation 边界明确，compatibility bridge 有命名、有归属、有删除条件；具体拆文件和降行数工作已由后置的 `REQ-076-d` 汇总执行。

## 目标

1. 把 `REQ-054-a` 的实施边界更新为“当前结构收口”，不再重复 `REQ-054-b` 已完成的 offline MVP。
2. 让 `VulkanRenderer` public facade 保持薄委托，不再承载新行为。
3. 把 `VulkanRealtimeRenderer` 的大文件拆分要求移交到 `REQ-076-j`，本 REQ 只定义拆分时不能破坏的 renderer 边界。
4. 建立 renderer 层 compatibility bridge 台账，明确每个 bridge 的 owner REQ、默认路径是否允许使用、删除条件和验证方式。
5. 保证 offline renderer 不依赖 realtime renderer、swapchain、GUI 或 editor frame loop。
6. 不接管 Material v3、RenderPath shader URI、OfflineRT graph path、package/canonical state 等后续 active REQ 的具体 hard cut，但本 REQ 必须防止 renderer refactor 扩大这些兼容路径。

## 非目标

- 不实现新的 offline integrator 算法。
- 不实现 Material v3 source storage、shader variant、indirect batching 或 realtime material hard cut；这些由 `REQ-073-b` 到 `REQ-073-e` 以及 `REQ-076-b` 承接。
- 不迁移 `techniques/...` 到 `render_paths/...`；这是 `REQ-073-d`。
- 不删除 OfflineRT 旧 provider / `offlineShader` / hardcoded FrameGraph；这是 `REQ-076-c` / `REQ-076-d`。
- 不实现 package、BC7、pipeline cache restore 或 post-package cleanup；这些由 `REQ-074-*` 承接。
- 不重命名 editor-facing `VulkanRenderer` facade，除非后续有单独 API cleanup 需求。
- 不执行大文件拆分；`src/backend/vulkan/vulkan_realtime_renderer.cpp` 的具体拆分由 `REQ-076-j` 承接。

## 需求

### R1: Thin Facade Boundary

`VulkanRenderer` SHALL 继续只是 editor-facing realtime facade。

要求：

- `VulkanRenderer` 只持有并委托 `VulkanRealtimeRenderer`。
- 不在 `VulkanRenderer` 中新增 offline 分支、validation profile 分支或 package restore 分支。
- `gpu::Renderer` 的 `initialize()`、`initScene()`、`uploadData()`、`draw()` 仍由 realtime renderer 实现。
- 所有新增 debug / dump / realtime profile API 如果仍挂在 facade 上，只能转发到 realtime renderer 或独立服务。

验收：

- `src/backend/vulkan/vulkan_renderer.cpp` 维持薄委托，不能重新膨胀为实现文件。
- 新增功能不得直接修改 facade 成为第二个大类。

### R2: Foundation Ownership

`VulkanRendererFoundation` SHALL 是共享 Vulkan runtime 的唯一 foundation 入口。

当前允许的职责：

| 职责 | 边界 |
|---|---|
| device lifecycle | realtime 使用 window/surface，headless 不创建 surface |
| command manager | graphics queue family 的 command buffer 管理 |
| resource manager | shader/pipeline/buffer/texture 资源管理入口 |
| shutdown order | wait idle 后释放 resource manager、command manager、device |

要求：

- foundation 不依赖 swapchain、GUI、scene runtime、editor session。
- headless foundation 的测试必须确认没有 surface。
- offline integrator 如果继续自己创建 device/resource manager，必须在状态中记录为临时重复 runtime；后续应收敛到 foundation 或明确保留原因。

验收：

- `test_vulkan_offline_renderer` 继续覆盖 headless foundation。
- realtime 初始化仍通过 `VulkanRendererFoundation::createRealtime()`。

### R3: Realtime Decomposition Handoff

`src/backend/vulkan/vulkan_realtime_renderer.cpp` 当前仍是 3000+ 行级主文件，但具体拆分不在本 REQ 内执行。

本 REQ 对 `REQ-076-j` 的约束：

- 新 helper 不得把 offline integrator、OfflineRT 旧 provider 或 headless runtime 接回 realtime renderer。
- 新 helper 不得新增 material/URI/offline 兼容 fallback。
- FrameGraph 执行、dump/profile output、post/skybox/shadow 等 helper 可以拆出，但必须继续通过 `VulkanRendererFoundation`、`VulkanResourceManager` 和显式 constructor injection 取得依赖。
- `VulkanRenderer` facade 仍只能转发，不因拆分重新承载实现细节。

### R4: Compatibility Bridge 台账

所有 renderer 层兼容路径 SHALL 有显式台账。台账可以先放在本 REQ 的实施状态，也可以落到独立 audit test。

最低记录字段：

| 字段 | 说明 |
|---|---|
| bridge 名称 | 例如 `offlineShader side channel`、`DescriptorResourceList batch equality` |
| 当前调用点 | 文件 / 函数 |
| 默认路径是否使用 | yes / no / validation-only |
| 删除归属 | `REQ-073-d/e/f/g/h`、`REQ-074-*` 或本 REQ |
| 删除条件 | 哪个正向路径完成后删除 |
| 验证 | rg/audit、unit test、integration test 或 smoke |

当前已知 bridge：

| Bridge | 当前调用点 | 删除归属 |
|---|---|---|
| `OfflineRenderJob::offlineShader` | `src/core/offline/offline_render_job.hpp`、`software_compute_offline_integrator.cpp` | `REQ-076-d` |
| hardcoded offline graph | `src/core/offline/offline_render_work_graph.cpp` | `REQ-076-c` / `REQ-076-d` |
| `Pass_OfflineRayTrace` compute item 特判 | `src/core/frame_graph/render_queue.cpp` | `REQ-076-d` |
| `techniques/...` shader URI | render path assets / shader resolver / tests | `REQ-073-d` |
| descriptor-resource equality as batch boundary | `src/core/frame_graph/render_queue.cpp` | `REQ-073-e` |
| old material fallback / non-bindless path | realtime submission and validation boundary | `REQ-076-b` |
| `BloomColor` implicit fallback read | `VulkanRealtimeRenderer::attachFrameGraphSampledResources()` | 本 REQ 或后续 RenderPathGraph cleanup |

本 REQ 不要求立即删除所有 bridge，但要求任何 renderer refactor 不得把 unnamed bridge 搬成更难删除的 helper。

### R5: Offline Renderer Boundary

offline renderer SHALL 保持和 realtime renderer 分离。

要求：

- `backend::offline::VulkanOfflineRenderer` 通过 offline integrator 处理 `OfflineRenderJob`。
- offline integrator 不继承 `gpu::Renderer`，不实现 `draw()` / present loop。
- offline path 不依赖 window、swapchain、GUI 或 editor state。
- offline 当前 `software-compute` integrator 内部自建 headless Vulkan runtime 是临时重复 runtime；如果继续保留，必须只在 offline namespace 内，不得回流到 realtime renderer。

与后续需求边界：

- OfflineRT 从 hardcoded graph 切到 RenderPathGraph 属于 `REQ-076-c`。
- 删除 `offlineShader` side channel 和 old provider bridge 属于 `REQ-076-d`。
- 本 REQ 只保证这些 bridge 不跨回 realtime/foundation 边界。

### R6: Realtime Submission Hard-cut Boundary

realtime renderer 的提交路径 SHALL 只表达 renderer 结构，不承担 Material v3 的最终 hard cut。

要求：

- `submitBindlessQueue()`、batch 编译、descriptor 绑定等逻辑拆分时，必须保留或加强现有 diagnostics。
- 若遇到 old material、per-material descriptor、non-bindless fallback、legacy URI，不得在新模块中新增 silent fallback。
- `REQ-073-e/f` 的正向测试需要的 renderer diagnostics 应在拆分后仍可定位到 pass、batch、pipeline key、material source 和 draw index。

验收：

- `test_bindless_indirect_contract`、`test_bindless_validation_contract`、RenderPathGraph parser/default graph tests 继续通过。
- 拆分后 unsupported non-batch submission 仍 fail-fast，不静默 draw。

### R7: Helper Boundary Contract

`REQ-076-j` 拆出 render target dump、profile output、IBL/post/shadow helper 时，SHALL 遵守本 REQ 的边界合同。

要求：

- debug dump、profile output、post/skybox/shadow helper 不拥有 swapchain 生命周期。
- IBL bake injection 与 scene-level environment resource sync 不读取 editor/session 全局状态。
- 当前 `pipeline_srgb.png` 仍 unavailable 的事实由 `REQ-068-a` 跟踪，本 REQ 不改变 output profile 语义。
- helper 只消费显式输入；如果必须穿透到 renderer 内部状态，需要先在 `REQ-076-j` 的职责审计中记录原因。

### R8: Tests And Audit

本 REQ 的验证以结构和边界为主。

建议命令：

```bash
cmake --build build --target \
  test_vulkan_offline_renderer \
  test_offline_integrator_selection \
  test_vulkan_frame_graph \
  test_vulkan_ibl_bake \
  test_default_forward_render_path_graph_source \
  test_default_deferred_render_path_graph_source \
  test_bindless_validation_contract \
  test_bindless_indirect_contract \
  test_realtime_render_profile_commands \
  lxe_editor -j2

ctest --test-dir build --output-on-failure -R \
  "test_(vulkan_offline_renderer|offline_integrator_selection|vulkan_frame_graph|vulkan_ibl_bake|default_forward_render_path_graph_source|default_deferred_render_path_graph_source|bindless_validation_contract|bindless_indirect_contract|realtime_render_profile_commands)"
```

如果运行环境没有 video device，Vulkan/windowed tests 按仓库约定使用 `xvfb-run -a`。

## 修改范围

- `src/backend/vulkan/vulkan_renderer.*`
- `src/backend/vulkan/vulkan_renderer_foundation.*`
- `src/backend/vulkan/vulkan_realtime_renderer.*`
- `src/backend/vulkan/vulkan_offline_renderer.*`
- `src/backend/vulkan/offline/*`
- `src/core/frame_graph/render_queue.*`
- `src/core/offline/*`
- 相关 CMake glob 自动纳入的 backend source
- Vulkan / FrameGraph / offline / bindless / realtime profile tests

## 边界与约束

- 不把 `VulkanRendererFoundation` 做成万能 service locator。
- 不把 offline integrator 接回 `gpu::Renderer` realtime 接口。
- 不把 073/074 的材质、URI、package hard cut 偷偷塞进 054-a。
- 不把兼容 bridge 搬进命名更干净但语义更隐蔽的新 helper。
- 不用本 REQ 执行降行数拆分；拆文件统一由 `REQ-076-j` 管理。

## 依赖

- 当前 Vulkan backend。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: Indirect material batching and diagnostics。
- `REQ-076-b`: Realtime material path hard cut and smoke。
- `REQ-076-c` / `REQ-076-d`: OfflineRT RenderPathGraph path and config hard cut。
- `REQ-074-*`: package / pipeline cache / post-package cleanup。
- `REQ-076-j`: large-file decomposition backlog。

## 后续工作

- 完成 073/074 后，再评估是否需要删除 `VulkanRenderer` facade 或重命名 top-level `vulkan_offline_renderer.*` headless foundation wrapper，避免和 `backend::offline::VulkanOfflineRenderer` 混淆。
- Vulkan hardware ray tracing backend 如进入 active，应单独起新 REQ，不复用本 REQ 的拆分范围。

## 实施状态

2026-06-14 重整后状态：部分完成，继续 active。

已完成：

- `VulkanRenderer` public facade 已缩到薄委托。
- `VulkanRealtimeRenderer` 已成为 realtime `gpu::Renderer` 实现。
- `VulkanRendererFoundation` 已支持 realtime/headless 初始化。
- top-level `VulkanOfflineRenderer` headless foundation smoke 已存在。
- `backend::offline::VulkanOfflineRenderer`、`software-compute` integrator、offline render graph executor 和 `lxe_offline_render` 已存在。
- `VulkanPostProcessBuilder`、`IblBakeRenderer`、`VulkanGpuResourceTable` 等局部 helper 已形成。

仍未完成：

- `vulkan_realtime_renderer.cpp` 仍是 3000+ 行级主文件；具体拆分已经移交 `REQ-076-j`，本 REQ 只保留 renderer 边界要求。
- `software-compute` integrator 仍自建 headless Vulkan runtime，尚未和 foundation 统一或明确保留原因。
- OfflineRT 仍有 `offlineShader`、hardcoded offline graph 和 `Pass_OfflineRayTrace` 特判，这些由 `REQ-076-c/d` 删除，但本 REQ 要保证它们不污染 renderer foundation/realtime 边界。
- realtime material / descriptor / batch 兼容路径仍在清理中，归属 `REQ-073-e/f`；本 REQ 需要在拆分时保留 diagnostics，不扩大 fallback。
- compatibility bridge 台账尚未落成测试或文档化状态表。
