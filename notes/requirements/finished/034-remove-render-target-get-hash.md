# REQ-034: 删除 RenderTarget::getHash dead code

> 本 REQ 在 2026-05-01 从 [REQ-042 RenderTarget 拆分](../042-render-target-desc-and-target.md) 抽取独立成文。在拆分前，REQ-042 既包含"立即可做的 cleanup（删 dead `getHash`）"又包含"后置的 RenderTarget 重写（拆 desc / 改 PipelineKey / 升级 `Camera::matchesTarget`）"，两段实施周期混在一个文件里不利于驱动。按"一个 REQ 文件 = 一个实施周期"的仓库规约（见 [`README.md`](../README.md)），本 REQ 承担前者，REQ-042 收敛为后者。
>
> 实施顺序上 cleanup 应该最先做，因此按"REQ 文件号 = 实施顺序"的规约本 REQ 取号 034（早于 035-041 Phase 1.5 工作 + 042 RenderTarget 重写）。

## 背景

`src/core/frame_graph/render_target.hpp` 中的 `usize getHash() const` 是 placeholder 期遗留 API。当前状态：

- **没有任何生产调用方**：grep 全工程，唯一引用点是 `src/test/integration/test_pipeline_build_info.cpp` 中的 `testRenderTargetHashStability` 函数 —— 该函数测的是它自己。
- **与仓库 identity 路径形态冲突**：全仓 identity 走 `getPipelineSignature() -> StringID`（[REQ-006](006-string-table-compose.md) / [REQ-007](007-interning-pipeline-identity.md)）；runtime hash 是反模式，留着会让新人误以为这是合法 identity 路径。
- **不阻塞任何工作**：删它是纯粹的 dead-code 清理。

## 目标

删除 `RenderTarget::getHash()` 与对应自测，让 `RenderTarget` 不再持有过时的 identity API。

## 需求

### R1: 删 `getHash` 声明与实现

- 删 `src/core/frame_graph/render_target.hpp` 中 `usize getHash() const;` 声明
- 删 `src/core/frame_graph/render_target.cpp` 中实现
- 若 `.cpp` 因此变空，整个 `.cpp` 文件可删，让 `RenderTarget` header-only

### R2: 删除自测

- 删 `src/test/integration/test_pipeline_build_info.cpp` 中：
  - `testRenderTargetHashStability` 函数定义
  - `main()` 中对它的调用
- 若该测试文件因此变空，文件可删；若仍有其他测试，保留

### R3: 落地后工程依然编译可测

- `ninja test_shader_compiler && ninja BuildTest` 全绿
- `ctest --output-on-failure -L auto -LE requires_video_device` 全绿
- `xvfb-run -a ctest --output-on-failure -L requires_video_device` 全绿
- grep 全仓 `getHash` 在 `RenderTarget` 范畴下零引用（`std::hash` 之类的 STL hash 不算）

## 测试

- 删除前：`grep -rn "getHash" src/ notes/source_analysis/` 仅命中 RenderTarget 路径下声明 / 实现 / 自测
- 删除后：以上三处零命中
- 整工程构建 + 测试链路（见 R3）全部通过

## 修改范围

- `src/core/frame_graph/render_target.hpp`
- `src/core/frame_graph/render_target.cpp`（可能整体删除）
- `src/test/integration/test_pipeline_build_info.cpp`
- `notes/source_analysis/src/core/frame_graph/render_target.md`（如该 md 文档提到 `getHash`，落地后顺便删一句）

## 边界与约束

- 仅删 dead API，**不**引入任何字段 / 设计变化
- **不**改 `RenderTarget` 现有任何字段（`colorFormat` / `depthFormat` / `sampleCount`）
- **不**改 `RenderTarget::operator==`（与字段层语义无关）
- **不**预判 [REQ-042](../042-render-target-desc-and-target.md) R1-R8 的设计；REQ-042 落地时如何处理 `RenderTarget` 字段表，由那边决定，与本 REQ 无关

## 依赖

无。本 REQ 不依赖任何前置工作，不阻塞任何后续工作。

## 后续工作

- [REQ-042](../042-render-target-desc-and-target.md) — RenderTarget 拆分为 desc + binding（Phase 1.5 完工后开工，Phase 1 REQ-103 立项前完成）

## 实施状态

2026-05-01 已落地并验证通过。

- `src/core/frame_graph/render_target.hpp` 删除 `usize getHash() const;`
- `src/core/frame_graph/render_target.cpp` 因仅承载 dead `getHash` 实现而整体删除，`RenderTarget` 回到 header-only
- `src/test/integration/test_pipeline_build_info.cpp` 删除 `testRenderTargetHashStability` 及 `main()` 中调用
- `notes/source_analysis/src/core/frame_graph/render_target.md` 去掉对已删除 `getHash` / `render_target.cpp` 的说明
- 验证 `rg -n "getHash" src notes/source_analysis` 零命中
- 构建与测试通过：
  - `cmake .. -G Ninja`
  - `ninja test_shader_compiler BuildTest`
  - `ctest --output-on-failure -L auto -LE requires_video_device`
  - `xvfb-run -a ctest --output-on-failure -L requires_video_device`
- 验证期间顺手修复一处既有测试漂移：`src/test/integration/test_material_variant_rules.cpp` 改为向当前仓库真实资源根 `assets/materials/` 写临时材质文件，避免继续依赖旧的根级 `materials/` 目录布局
