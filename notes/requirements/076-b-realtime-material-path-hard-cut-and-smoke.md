# REQ-076-b: Transparent BMW Material Path And Smoke

> 2026-06-14 重新收束：旧 realtime material fallback hard cut 已前移到 `REQ-073-d` 和 `REQ-073-e`。本 REQ 不再清理 073a/b/c 的旧兼容路径；它只在 Task 8/9 后的 `RenderPathGraph input -> FramePass input contract -> RenderWorkCompiler -> typed RenderInput[] -> RenderInputDesc[] facts -> backend execution` 模型上扩展 transparent sorting policy、glass material、BMW converter/shader 覆盖和 BMW realtime smoke。

## 背景

`REQ-073-e` / `REQ-073-e2` 已把 realtime geometry 默认路径收束为：

```text
RenderPathGraph input
  -> FramePass input contract
  -> RenderWorkCompiler
  -> typed RenderInput[]
  -> RenderInputDesc[] validation / pipeline / binding / resource / diagnostic / stats facts
  -> backend execution
```

draw / dispatch execution data 留在 typed `RenderInput` 侧；`RenderInputDesc` 只用 `inputIndex` 指回对应 input，并保存 validation、pipeline、binding、resource dependency、diagnostic 和 stats facts。

BMW M6 相比 Helmet 多出透明/玻璃相关材质。透明物体不能简单按 locality 任意重排；它们需要先满足 back-to-front depth ordering，再在相邻 compatible typed raster inputs 间做内部聚合。因此 transparent sorting / aggregation 应该是 `RenderWorkCompiler` raster policy 的扩展，而不是 BMW-only shortcut、第二套 renderer 或 public parallel compiler。

## 目标

1. 增加 transparent RenderPathGraph pass / RenderPathNode contract。
2. 在 `RenderWorkCompiler` raster policy 上实现 transparent sort policy。
3. transparent typed raster inputs 先按 depth back-to-front 排序，再只对相邻且 compatible 的 inputs 做内部聚合。
4. 支持 BMW 中需要的 glass material contract、converter 参数、source-local material storage 和 shader variant。
5. BMW converter 遇到目前未建模但 BMW 存在的材质时，要么显式支持，要么 fail-fast diagnostic；不能回退 debug/default/opaque approximation。
6. BMW realtime smoke 输出非全黑，或对明确未支持 feature 输出可解释 diagnostic；不得使用旧 fallback。

## 非目标

- 不重新打开 073e opaque batching 架构。
- 不引入透明专用的第二套 batch compiler。
- 不实现 package、BC7、pipeline cache serialization。
- 不处理 OfflineRT compute path；由 `REQ-076-c` / `REQ-076-d` 处理。
- 不做 offline/realtime 图像等价阈值；由 `REQ-076-f` 处理。

## 需求

### R1: Transparent RenderPathGraph Pass

RenderPathGraph SHALL 显式声明 transparent pass。

要求：

- transparent pass 有独立 pass id / RenderPathNode。
- pass contract 明确 shader URI、sources、targets、attachments、geometry contract、render state 和 blending。
- transparent pass 选择不能来自 material name、implicit alpha heuristic 或 converter-side shortcut。
- 不支持的透明材质必须 fail-fast，不得塞进 opaque pass 近似渲染。

### R2: Transparent Sort Policy

transparent pass SHALL 使用当前单轨 compiler 模型：RenderPathGraph `input` 声明 transparent scene renderables，FramePass 保存 pass/input contract，`RenderWorkCompiler` 生成 transparent `RenderDrawInput[]` 或等价 typed raster input，再输出 `RenderInputDesc[]` facts。

排序/内部聚合规则：

```text
transparent:
  build typed raster RenderInput[]
  sort back-to-front by camera depth
  aggregate only adjacent compatible inputs inside the raster policy
  emit RenderInputDesc[] facts with inputIndex, pipeline/binding/dependency facts, diagnostics and stats
```

compatibility 仍来自结构性事实，例如：

```text
object data signature + material type / pipeline signature
```

不得引入 transparent-only / opaque-only public compiler，也不得恢复旧 public batch result。aggregation 可以是 `RenderWorkCompiler` raster policy 的内部行为，但 public output 仍是 typed `RenderInput[]` 加 `RenderInputDesc[]` facts。

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
- opaque pass 继续走 `RenderWorkCompiler` / `RenderInputDesc` path。
- transparent pass 显式存在并执行。
- transparent typed inputs depth sorted。
- adjacent compatible transparent inputs may be internally aggregated by the raster policy。
- glass material shader/source storage 被实际使用。
- output non-black，或明确 unsupported diagnostic。
- fallback-observed count 为 0。

## 测试

### T1: Transparent Pass Contract

解析 BMW/forward render path，断言 transparent pass 显式存在，且 pass contract 包含 shader URI、blend state、target/attachment 和 geometry contract。

### T2: Transparent Sorting

构造多个透明对象，断言 typed raster inputs 排序为 back-to-front。相同 compatibility signature 但排序后不相邻的对象不得跨越中间对象聚合。

### T3: Transparent Adjacent Aggregation

构造 depth order 中相邻且 compatibility signature 相同的透明对象，断言它们可以被 `RenderWorkCompiler` raster policy 内部聚合，同时 public output 仍通过 typed input 和 `RenderInputDesc` facts 表达。

### T4: Glass Material Conversion

用 BMW 中的 glass material fixture 跑 converter，断言参数进入 Material v3 source-local storage，final shader reflection 能看到对应 accessors。

### T5: Unsupported Material Diagnostic

构造 BMW 中存在但本 REQ 不支持的 material feature，断言 converter 或 validation fail-fast，不能替换成 default/debug/opaque material。

### T6: BMW Realtime Smoke

运行低分辨率 BMW realtime smoke，断言非黑图或明确 unsupported diagnostic，并校验 `renderInputStats`、submitted draw coverage、glass usage、`fallback-observed == 0`。

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
- `RenderWorkCompiler` transparent node sort policy usage
- `RenderWorkCompiler` transparent policy tests
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

- `REQ-076-c`: OfflineRT RenderPathGraph compute path。
- `REQ-076-d`: OfflineRT config hard cut and smoke。
- `REQ-074-a`: Texture compression pipeline with BC7。

## 实施状态

未实施。
