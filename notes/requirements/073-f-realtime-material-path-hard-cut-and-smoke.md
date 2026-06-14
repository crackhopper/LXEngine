# REQ-073-f: Transparent BMW Material Path And Smoke

> 2026-06-14 重新收束：旧 realtime material fallback hard cut 已前移到 `REQ-073-d` 和 `REQ-073-e`。本 REQ 不再清理 073a/b/c 的旧兼容路径；它只在 073e 的 node-level batching / indirect submission 模型上扩展 transparent sorting/batching、glass material、BMW converter/shader 覆盖和 BMW realtime smoke。

## 背景

`REQ-073-e` 会让 realtime geometry 默认路径使用：

- `RenderPathNodeContext` / `RenderPathNodeData`
- `RenderDrawCandidate`
- `RenderBatchCompiler`
- `RenderBatchAnalysis`
- object data signature + material pipeline signature
- Vulkan indirect draw submission

BMW M6 相比 Helmet 多出透明/玻璃相关材质。透明物体不能简单按 batch locality 任意重排；它们需要先满足 back-to-front depth ordering，再在相邻 compatible candidates 间合批。因此 transparent batching 应该是 073e 通用 compiler 的 policy 扩展，而不是 BMW-only shortcut 或第二套 renderer。

## 目标

1. 增加 transparent RenderPathGraph pass / RenderPathNode contract。
2. 在 073e 的 `RenderBatchCompiler` 模型上实现 transparent sort policy。
3. transparent candidates 先按 depth back-to-front 排序，再只合并相邻且 batch signature 相同的 candidates。
4. 支持 BMW 中需要的 glass material contract、converter 参数、source-local material storage 和 shader variant。
5. BMW converter 遇到目前未建模但 BMW 存在的材质时，要么显式支持，要么 fail-fast diagnostic；不能回退 debug/default/opaque approximation。
6. BMW realtime smoke 输出非全黑，或对明确未支持 feature 输出可解释 diagnostic；不得使用旧 fallback。

## 非目标

- 不重新打开 073e opaque batching 架构。
- 不引入透明专用的第二套 batch compiler。
- 不实现 package、BC7、pipeline cache serialization。
- 不处理 OfflineRT compute path；由 `REQ-073-g` / `REQ-073-h` 处理。
- 不做 offline/realtime 图像等价阈值；由 `REQ-075-a` 处理。

## 需求

### R1: Transparent RenderPathGraph Pass

RenderPathGraph SHALL 显式声明 transparent pass。

要求：

- transparent pass 有独立 pass id / RenderPathNode。
- pass contract 明确 shader URI、sources、targets、attachments、geometry contract、render state 和 blending。
- transparent pass 选择不能来自 material name、implicit alpha heuristic 或 converter-side shortcut。
- 不支持的透明材质必须 fail-fast，不得塞进 opaque pass 近似渲染。

### R2: Transparent Sort Policy

transparent node SHALL 使用 073e 的 `RenderPathNodeContext` / `RenderPathNodeData` / `RenderBatchCompiler`。

排序/合批规则：

```text
transparent:
  collect RenderDrawCandidate[]
  sort back-to-front by camera depth
  merge only adjacent candidates with the same batch signature
```

batch signature 仍是：

```text
object data signature + material pipeline signature
```

不得引入 `TransparentBatchCompiler`、`OpaqueBatchCompiler` 或其它与 `RenderBatchCompiler` 并行的 public compiler。

### R3: Glass Material Contract

BMW glass material SHALL 显式映射为 Material v3 source contract。

最低支持：

- transmission / opacity 或当前 glass shader 需要的等价参数。
- roughness / tint / IOR 等 BMW 中出现且 shader 会消费的参数。
- texture slot 和 factor 都进入 source-local material storage。
- shader reflection 使用 final material-source variant。

如果某个 BMW material source 暂不支持，converter 必须输出 unsupported diagnostic，并保证该 draw 不进入 positive smoke 的成功路径。

### R4: Transparent Shader Variant

transparent pass shader SHALL 使用 `render_paths/...` URI 和 Material v3 accessor ABI。

要求：

- 不使用 `techniques/...`。
- 不读取旧 `MaterialUBO` / `SceneGpuMaterialRecord` PBR truth。
- 不使用 per-material descriptor fallback。
- 不因 texture presence 或材质参数值拆 batch；这些差异通过 table index/source-local storage 表达。

### R5: BMW Converter Coverage

BMW converter SHALL 处理 BMW 中实际出现的材质类别。

要求：

- glass 材质映射到 explicit source contract。
- unsupported material feature 有 material/source/parameter 级 diagnostic。
- converter 不得把未知材质静默替换为 opaque/default/debug material。
- conversion output 的 positive fixture 只引用 `render_paths/...` 和 Material v3 source contract。

### R6: BMW Realtime Smoke

BMW realtime smoke SHALL 验证：

- converted BMW scene loads。
- opaque pass 继续走 073e indirect batching。
- transparent pass 显式存在并执行。
- transparent candidates depth sorted。
- adjacent compatible transparent candidates may batch。
- glass material shader/source storage 被实际使用。
- output non-black，或明确 unsupported diagnostic。
- fallback-observed count 为 0。

## 测试

### T1: Transparent Pass Contract

解析 BMW/forward render path，断言 transparent pass 显式存在，且 pass contract 包含 shader URI、blend state、target/attachment 和 geometry contract。

### T2: Transparent Sorting

构造多个透明对象，断言排序为 back-to-front。相同 batch signature 但排序后不相邻的对象不得跨越中间对象合批。

### T3: Transparent Adjacent Batching

构造 depth order 中相邻且 batch signature 相同的透明对象，断言它们可以进入同一 indirect batch。

### T4: Glass Material Conversion

用 BMW 中的 glass material fixture 跑 converter，断言参数进入 Material v3 source-local storage，final shader reflection 能看到对应 accessors。

### T5: Unsupported Material Diagnostic

构造 BMW 中存在但本 REQ 不支持的 material feature，断言 converter 或 validation fail-fast，不能替换成 default/debug/opaque material。

### T6: BMW Realtime Smoke

运行低分辨率 BMW realtime smoke，断言非黑图或明确 unsupported diagnostic，并校验 opaque/transparent batch stats、glass usage、`fallback-observed == 0`。

### T7: rg Concept Drift Audit

实现完成报告必须包含 rg 审计：

```bash
rg -n "OpaqueBatch|OpaqueGeometry|OpaqueIndirect|TransparentBatchCompiler" src/core src/backend src/test
rg -n "techniques/|MaterialUBO|SceneGpuMaterialRecord|defaultTechnique|material-local technique" assets src/core src/backend src/infra src/test
rg -n "debug material|default material|opaque approximation|fallback-observed" src/core src/backend src/infra src/test
```

ordinary positive tests 和 production path 不得保留这些 token 作为成功路径。允许的 negative audit / diagnostic hit 必须在完成报告中列出。

## 修改范围

- `assets/render_paths/*.render-path.yaml`
- transparent/glass shaders under `assets/shaders/glsl/render_paths/`
- BMW converter and material mapping code
- `RenderWorkQueue` transparent node sort policy usage
- `RenderBatchCompiler` transparent policy tests
- SceneResourceTable material/source storage coverage
- BMW realtime smoke and diagnostics

## 边界与约束

- 不留下 opaque-only / transparent-only 两套 compiler。
- 不通过 material name 或 alpha heuristic 选择 pass。
- 不用旧 material truth、per-material descriptor、debug/default material 或 skipped draw 证明 smoke 成功。
- 不把 package / BC7 / pipeline cache 工作提前塞入本 REQ。

## 依赖

- `REQ-073-b`: bindless-ready material/object/draw/mesh tables。
- `REQ-073-c`: material source shader variant and final shader reflection。
- `REQ-073-d`: RenderPath shader URI migration and terminology hard cut。
- `REQ-073-e`: RenderPathNode batching, diagnostics and indirect submission。

## 后续工作

- `REQ-073-g`: OfflineRT RenderPathGraph compute path。
- `REQ-073-h`: OfflineRT config hard cut and smoke。
- `REQ-074-a`: Texture compression pipeline with BC7。

## 实施状态

未实施。
