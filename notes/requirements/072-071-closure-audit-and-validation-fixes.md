# REQ-072: REQ-071 Closure Audit And Validation Fixes

> 2026-06-11 新增：本文档记录对 `REQ-071-a` 到 `REQ-071-f` 需求与当前代码的复核结果。结论是：071 已提交了一批 material/resource/rendering pipeline 基础设施，但尚未满足 071 自己定义的“完整迁移 + 最终验收”标准。REQ-072 的目标是把这些问题转成一个连续修复周期，避免它们继续以“071 已完成”的状态隐性存在。

## 背景

`REQ-071` 的总目标是一次性完成：

```text
source scene / package
  -> SceneResourceTable
  -> technique validation + FrameGraph
  -> GPUResourceTable upload/preload
  -> bindless + indirect realtime/offline execution
  -> helmet/BMW direct-lighting validation
```

当前代码中已经存在部分基础设施，例如 Material v2 parser、resource graph export、`IGpuResourceTable` 接口、indirect batch bridge、scene package manifest wrapper、validation profile parser 和 diagnostics compare 分类。但复核发现这些实现多数仍是 contract shell 或局部桥接，未达到 `071-d/e/f` 的默认路径要求。

## 审阅结论

071 不能按“需求完全完成”归档。必须先完成本 REQ 中列出的阻塞项，再重新运行 071 最终 gate，并更新 071 各文档实施状态。

## 问题清单

### P0-1: 071 最终验证未通过

证据：

- `docs/superpowers/plans/2026-06-10-071-material-resource-rendering-pipeline.md` 的最终 F gate 记录为 partial：headless `ctest` 74/75，requires-video-device `ctest` 17/18。
- 剩余失败 1：`test_offline_render_cli` 的 `software-compute render should produce finite pixels`。
- 剩余失败 2：`test_realtime_offline_compare_flat` 通过 `lxe_realtime_render` 触发 editor 连接关闭；手动 gdb 复现记录为 llvmpipe/Vulkan 路径中 `VulkanRealtimeRenderer::generateRealtimeProfileOutput()` 调到 `VulkanCommandBufferManager::endSingleTimeCommands()` 附近 SIGSEGV。

影响：

- `REQ-071-f` 要求 headless realtime、editor export、offline direct 能在 direct profile 下稳定输出并比较。当前仍有一个 offline 有限像素失败和一个 realtime headless 崩溃，不能作为 071 验收通过。

### P0-2: 071-a 到 071-f 文档实施状态仍是“未实施”

证据：

- `notes/requirements/071-a-material-v2-pbrt-surface-contract.md`、`071-b`、`071-c`、`071-d`、`071-e`、`071-f` 的“实施状态”仍写“未实施”。
- `docs/superpowers/plans/2026-06-10-071-material-resource-rendering-pipeline.md` 的 closure task 仍未完成，要求更新每个 071 文件的完成行为、验证命令和 bridge audit outcome。

影响：

- 需求文档与代码提交状态不一致。后续 agent 会同时看到“已提交 071 实现”和“071 未实施”，无法判断当前事实。

### P0-3: VulkanGpuResourceTable 是空壳，不满足 071-d

证据：

- `src/backend/vulkan/vulkan_gpu_resource_table.cpp` 中 `createBuffer` / `createImage` / `createSampler` 只递增 id，不创建 Vulkan buffer/image/sampler。
- `updateBuffer` 丢弃 handle、offset 和 data。
- `updateBindlessSlot` 忽略 table/image/sampler 并固定返回 slot `0`。
- `findPipeline` 永远返回 `std::nullopt`，`getOrCreatePipeline` 只返回新 id，不接入真实 Vulkan pipeline cache。
- `queryProgress` 返回空进度。

影响：

- 不满足 `REQ-071-d` 对 Vulkan GPUResourceTable、bindless table、pipeline cache、cache blob、upload task progress 的要求。
- 现有测试只能证明接口形状，不能证明资源真的上传、slot 真的分配、pipeline 真的缓存。

### P0-4: 默认渲染仍会回落到非 bindless / 非 indirect draw path

证据：

- `RenderWorkQueue::compileIndirectBatches()` 跳过带 `raster.drawData` 的 draw item。
- `VulkanRealtimeRenderer::drawPassQueue()` 只有当 indirect batch 覆盖所有 items 时才走 `RasterBatch`，否则逐 item 调用 `cmd.executeWorkItem(item)`。
- `VulkanCommandBuffer::bindResourcesWithLayout()` 仍按每个 draw item 绑定 descriptor set、vertex/index buffer，并通过 push constants 写 `raster.drawData`。

影响：

- `REQ-071-d` 要求默认 editor/offline/headless tests 走 bindless + indirect，不能静默回退到旧 per-draw submission。
- 当前实现是“全覆盖时批处理，否则旧路径”，且真实场景中 per-draw push constants 会导致 batch 被跳过。bridge audit 的覆盖范围不足。

### P0-5: Scene package 只实现 manifest byte wrapper，不满足 071-e 的快速加载合同

证据：

- `src/core/package/scene_package_manifest.cpp` 使用 `LXPKG001` 包一段文本 manifest。
- 当前 package 记录 resource metadata/dependencies/hash，但没有 `.lxpkg` header + section table + chunk table + typed resource sections。
- 没有 streaming loader，没有 package-internal resolver，没有 package restore 重建 `SceneResourceTable` typed storage。
- 071 计划中的 E2 backend cache fallback、source/package offline equivalence、cache metadata sections 仍未勾选。

影响：

- 不满足 `REQ-071-e` 的单文件二进制容器、section/chunk 粒度、避免整体读入、source/package hash/render equivalence、backend cache metadata 等要求。
- 当前测试不能证明“删除 source YAML/material/mesh 后 package 仍可恢复并渲染”。

### P0-6: Helmet/BMW direct validation assets 和 per-material sphere tests 未完成

证据：

- 071 计划 F3 的 material sphere validation、full model validation、headless vs editor export、GBuffer/FrameGraph dump、helmet/BMW assets 更新均未勾选。
- `REQ-071-f` 要求每个 helmet/BMW `MaterialInstance` 生成 64x64 sphere case，并对 Forward / Deferred / OfflineRT 做 direct validation；当前没有对应验收测试。

影响：

- 当前只验证了 synthetic diagnostics compare 和部分 profile parser，未验证 071 最终目标的真实 helmet/BMW 资产链路。

### P1-1: validation profile parser 未覆盖 071-f 示例字段

证据：

- `src/infra/scene_io/scene_validation_profile.cpp` 从 `renderValidation` 只读取 `sourceMode`、`scenePath`、`packagePath`、`activeTechnique`、`shadows`、`ibl`、`transparency`。
- 071-f 示例中的 `activeCamera`、`outputProfile`、`randomSeed`、`toneMapping`、`compare.materialMasks`、`compare.diagnostics`、`debugDumps.frameGraphTargets`、`debugDumps.gbuffer` 等字段没有按示例结构完整解析。
- parser 当前从顶层 `toneMapping` 读取 tone mapping，而 071-f 示例把 tone mapping 放在 `renderValidation` / `offlineRender` profile 语义里。

影响：

- validation profile 不能完整固定 source mode、camera、direct profile、debug dump、compare controls 和 deterministic render settings。
- 后续 direct equivalence 测试即使运行，也可能缺少必要 metadata。

### P1-2: diagnostics-aware compare 仍缺 EXR/sidecar 实际接入和 per-material gate

证据：

- 071 计划 F2 记录“第一实现 consumes typed diagnostic buffers from tests/runtime callers; EXR sidecar channel loading can be layered on later”。
- `REQ-071-f` 要求 compare 工具输入 color EXR 和辅助 diagnostic buffers，输出 per-material/per-object metrics、top-N suspicious samples，并按 material interior / BRDF mismatch gate 判失败。

影响：

- 当前 synthetic buffer 分类不足以支撑 helmet/BMW 自动验收。
- `lxe_compare_exr` CLI 仍需要能加载实际 diagnostic outputs，并把分类结果纳入 gate。

### P1-3: 旧 PBR / MaterialUBO.baseColor 路径仍是 editor 和资产链路的一等路径

证据：

- `src/demos/lxe_editor/scene_runtime.cpp`、`scene_builder.cpp`、`core/editor/commands/builtin_commands.cpp`、`core/editor/inspector_panel.cpp` 仍大量使用 `MaterialUBO.baseColor` / `nodeMaterial.baseColor`。
- `src/infra/scene_asset/gltf_scene_asset_loader.cpp`、`scene_material_loader.cpp` 仍把 glTF/PBR 数据写到 `MaterialUBO.baseColorFactor`、`metallicFactor`、`roughnessFactor`。
- 多个资产仍包含 `MaterialUBO.baseColorFactor`、`metallicFactor`、`roughnessFactor` 或旧 `baseColor`。

影响：

- `REQ-071-a/d/f` 要求 old runtime PBR parameter truth 不再作为 migrated runtime material truth 或 smoke 条件。当前旧路径仍是默认 editor/material editing 和部分 scene asset load 的主要路径。
- 需要区分“保留旧 demo/debug asset 支持”和“071 migrated validation path 禁止触达”的边界，并用 audit test 固化。

## 目标

1. 修复 071 最终验证失败，重新跑完整 BuildTest、auto ctest、requires-video-device ctest。
2. 把 071 的实际完成状态、剩余 bridge 和验证结果写回 071 文档。
3. 将 `VulkanGpuResourceTable` 从空壳推进到真实 Vulkan resource/cache/upload 接入，或明确降级 071-d scope 并更新需求。
4. 让默认 validation 渲染路径不能静默回退到非 bindless / 非 indirect draw。
5. 完成 scene package 的真实 `.lxpkg` section/chunk/restore/cache metadata 最小闭环，或把 071-e 重命名为 manifest-only 并拆出真实 package 后续需求。
6. 完成 helmet/BMW material sphere、full model、source/package、headless/editor export direct validation。
7. 清理或隔离旧 PBR / legacy descriptor / non-bindless bridge，默认 validation 不得触达。

## 需求

### R1: 修复最终验证阻塞

- 修复 `test_offline_render_cli` 的 finite pixels 失败，或者把 software-compute/llvmpipe 的不可达条件转成明确 skip/unsupported diagnostic，不能作为普通失败留在最终 gate。
- 修复 `test_realtime_offline_compare_flat` 的 llvmpipe/Vulkan SIGSEGV；如果是 driver/backend capability 问题，测试必须在 capability probe 阶段输出 unsupported，而不是让 editor remote close。
- 最终 gate 必须重新运行并记录：

```bash
cmake --build build --target BuildTest
ctest --test-dir build --output-on-failure -L auto -LE requires_video_device
xvfb-run -a ctest --test-dir build --output-on-failure -L requires_video_device
```

### R2: 收敛 071 文档状态

- 更新 `notes/requirements/071-a` 到 `071-f` 的“实施状态”。
- 每个 071 文档必须区分：已实现、部分实现、未实现、被 072 接管。
- 更新 `docs/superpowers/plans/2026-06-10-071-material-resource-rendering-pipeline.md` 的未完成 checkbox 和最终验证结果。

### R3: GPUResourceTable 真实化

- `VulkanGpuResourceTable` 必须创建真实 buffer/image/sampler 或复用 `VulkanResourceManager` 作为实现细节。
- bindless slot 分配必须按 resource identity 去重，不得固定返回 slot `0`。
- pipeline `find` / `getOrCreate` 必须接入真实 cache，并能观测 miss。
- `queryProgress` 必须反映 upload/preload task 状态。
- 新增或增强测试，证明数据上传、slot 去重、pipeline cache hit/miss、cache import/export 和 progress 都不是空壳。

### R4: 默认 validation 禁止 legacy draw fallback

- validation profile 下，如果 raster work item 无法进入 bindless indirect path，必须 fail-fast 或输出 unsupported，不得自动逐 item 走旧 submission。
- bridge audit 要覆盖真实 `VulkanRealtimeRenderer::drawPassQueue()` 行为，而不只是 synthetic queue。
- 如果某些 editor/debug pass 必须保留旧路径，必须挂显式 debug flag，默认关闭，并有测试证明 validation 不触达。

### R5: Scene package 最小真实闭环

- 定义并实现 `.lxpkg` header、section table、chunk table、resource metadata、dependency graph、typed sections、MaterialTemplate grouping、backend cache metadata。
- package loader 必须能在 source scene/material/mesh YAML 不可用时恢复 `SceneResourceTable` persisted state。
- loader 不得默认一次性读入完整 package；大型 section/chunk 要边读边恢复，并输出 progress。
- source parse 和 package restore 必须 root hash 一致；同一 scene 的 offline direct deterministic payload 必须一致。
- backend cache compatibility mismatch 要 warning 并 rebuild pipeline。

### R6: Helmet/BMW direct validation 补齐

- 为 helmet/BMW 实际存在的每个 `MaterialInstance` 生成 64x64 material sphere validation。
- 128x128 full model validation 覆盖 source/package、Forward/Deferred/OfflineRT direct 输出。
- headless realtime 输出必须与 editor export 输出比较，并带 technique、FrameGraph、pipeline key、resource table root hash、render settings hash metadata。
- Deferred GBuffer/FrameGraph dump 必须包含 logical target id/version 和 producer metadata。

### R7: Validation profile 和 compare 工具补齐

- profile parser 覆盖 071-f 示例中的 sourceMode、activeCamera、outputProfile、randomSeed、toneMapping、compare、debugDumps、shadow/IBL/transparency disables。
- `lxe_compare_exr` CLI 支持读取实际 diagnostic EXR channels 或 sidecar typed binary buffers。
- compare report 输出 per-material/per-object metrics、edge/coverage/input/BRDF/unsupported 分类和 top-N suspicious samples。
- gate 按 material interior / BRDF mismatch 判失败，edge/coverage 单独统计。

### R8: 旧 PBR / bridge 边界固化

- 迁移 071 validation assets 到 Material v2 + explicit techniques。
- 旧 PBR / `MaterialUBO.baseColor` editor helpers 如保留，必须标为 legacy/debug 或非 071 migrated path。
- 默认 helmet/BMW validation 不得加载旧 material truth、不得使用 legacy per-material descriptor path、不得用非 bindless draw submission 作为通过条件。

## 测试

### T1: Final Gate

完整运行 R1 的三个命令，全部 PASS 或明确 unsupported skip。

### T2: GPUResourceTable Contract Is Real

构造 texture/sampler/material/object scene：

- 相同 CPU texture handle 只分配一个 bindless slot。
- buffer/image/sampler 在 Vulkan backend 有真实资源对象。
- pipeline preload 后 `find` 命中。
- cache blob 导出/导入有可观测行为。

### T3: No Fallback Validation Path

在 validation profile 下构造一个不能 indirect batching 的 draw item：

- 测试应失败为 unsupported 或 contract violation。
- 不允许静默调用逐 item legacy draw path。

### T4: Package Restore Without Sources

构建 `.lxpkg` 后移动 source scene/material/mesh：

- package load 成功。
- root hash 与 source parse hash 匹配。
- offline direct deterministic payload 匹配。
- loader progress 显示 section/chunk restore。

### T5: Helmet/BMW Direct Validation

helmet/BMW：

- 每个 material sphere case 都有 Forward/Deferred/OfflineRT direct 输出和 diagnostic buffers。
- full model source/package 都非黑。
- headless/editor export 比较通过。
- compare report 给出 per-material metrics 和 suspicious samples。

## 修改范围

- `src/backend/vulkan/`：GPUResourceTable、resource manager/cache 接入、realtime validation crash 修复。
- `src/core/frame_graph/`：validation mode 下的 indirect fallback 策略和 bridge audit。
- `src/core/package/`、`src/infra/scene_io/`：真实 `.lxpkg` section/chunk/package restore。
- `src/tools/lxe_compare_exr/`、`src/tools/lxe_realtime_render/`、`src/tools/lxe_offline_render/`：diagnostic IO、headless/editor export compare、profile 接入。
- `assets/scenes/`、`assets/materials/`、BMW/helmet converted assets：Material v2 validation assets。
- `notes/requirements/071-*.md`、`docs/superpowers/plans/2026-06-10-071-material-resource-rendering-pipeline.md`：状态回写。

## 边界

- 本 REQ 不要求实现 shadows、IBL、transparent/glass correctness。
- 本 REQ 不要求 Fourier BSDF 精确求值，但必须保留数据并输出 disabled/unsupported diagnostic。
- 本 REQ 可以保留旧 editor debug/demo 路径，但 071 validation 默认路径必须证明不依赖它们。

## 实施状态

未实施。本文档是 2026-06-11 对 REQ-071 与当前代码对比后的问题收口清单，作为下一轮连续修复入口。
