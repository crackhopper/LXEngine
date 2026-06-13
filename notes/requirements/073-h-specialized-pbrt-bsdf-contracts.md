# REQ-073-h: Specialized PBRT BSDF Contracts

> 2026-06-13 拆分：`REQ-073-a` 已完成 Material v3 source contract、metallic realtime extension、PBRT 参数保留和 unsupported 诊断边界，但没有承诺完整 PBRT BSDF 物理模型。本 REQ 专门承接 `glass`、`fourier`、`mix`、conductor `eta/k` 等高阶 PBRT 材质表达，避免把这些后续物理模型塞回 073-a/b 的 clean architecture gate。

## 背景

`REQ-073-a` 的目标是建立材质合同：材质文件通过 `type.source` / `bsdf.source` 指向 source contract，source contract 反射参数 schema、source signature 和 Material Accessor ABI。这个合同已经足够支撑 metallic/roughness、默认纹理、source-reflected material records 和后续 bindless/indirect 路径。

PBRT 资产里还存在更复杂的材质语义：

- `glass` 的透射、折射率和粗糙玻璃模型。
- `fourier` 的表格 BSDF。
- `mix` / layered material 的复合与权重。
- conductor 的频谱或 RGB `eta/k`。
- 其它不能用当前 metallic realtime source 准确表达的 source。

这些语义如果在 073-a 里静默近似，会污染合同：下游 renderer 可能以为自己已经支持完整 PBRT BSDF，实际只是旧默认材质或不完整投影。因此，本 REQ 把“完整 PBRT BSDF source contract”独立出来。未实现前，导入器和渲染路径必须继续 fail-fast 或输出明确 unsupported/approximation diagnostic。

## 目标

1. 为高阶 PBRT BSDF 定义独立 material source contract。
2. 每个新增 source 都实现统一 Material Accessor ABI，避免 pass shader 写 runtime source 分支。
3. converter 对无法准确表达的 PBRT 参数输出明确 unsupported 或 approximation diagnostic。
4. 保留原始 PBRT 参数，禁止静默丢弃 `eta/k`、fourier table、mix 权重等信息。
5. 为 supported source 提供 shader variant 编译、反射和基础视觉/数值验证。

## 非目标

- 不阻塞 `REQ-073-b` 到 `REQ-073-g` 的 clean material/render/offline path。
- 不回滚 `REQ-073-a` 的 metallic realtime source contract。
- 不在 pass shader 中添加 material type/source runtime branch。
- 不实现 package 文件格式、BC7 压缩或 pipeline cache blob。
- 不要求一次性支持 PBRT 全部材质；未支持项必须明确诊断。

## 需求

### R1: Specialized Source Contract Set

为高阶 PBRT 材质定义 source contract。

首批候选：

| PBRT 材质 | source contract 方向 | 最低要求 |
|---|---|---|
| `glass` | dielectric / glass source | 反射率、透射率、ior、roughness、thin / solid 语义明确 |
| conductor `eta/k` | conductor source | 保留 eta/k 输入；明确 RGB 或频谱投影规则 |
| `mix` | composite source 或 explicit unsupported | 支持前不得把子材质静默拍平成 metallic |
| `fourier` | table BSDF source 或 explicit unsupported | table 文件缺失、格式不支持或 shader 不支持时 fail-fast |

每个 source contract SHALL 定义：

- source URI。
- source signature。
- 参数 schema。
- storage layout。
- Material Accessor ABI 实现。
- 支持的 RenderPath / pass 列表。
- unsupported / approximation diagnostics。

### R2: No Runtime Source Branch

新增 source SHALL 通过 `REQ-073-c` 的 shader variant 机制进入 pass shader。

要求：

- pass shader 继续调用统一 Material Accessor ABI。
- source-specific 结构、采样和 helper 由 contract source 提供。
- 不允许在 Forward、Deferred 或 OfflineRT pass shader 中写 `switch source`。
- 同 source signature 的 material record layout 必须一致；若发现冲突，诊断 bug 原因并停止，不写 workaround。

### R3: Converter Preservation And Diagnostics

PBRT converter SHALL 保留原始高阶材质参数，并输出明确诊断。

规则：

- 支持的 source：写入对应 `bsdf.source` 和 source 参数。
- 暂不支持的 source：写入可审计 unsupported diagnostic；不得伪装成 metallic 成功。
- 明确允许近似的 source：必须记录 approximation profile、丢失的信息和目标 source。
- `eta/k`、fourier table URI、mix 子材质引用和权重不得静默丢弃。

### R4: Source Reflection Validation

材质文件加载后 SHALL 通过 source reflection 校验高阶参数。

要求：

- 必填参数缺失时 fail-fast。
- 类型、范围、texture/channel selector 不匹配时 fail-fast。
- source 不支持目标 RenderPath 时 fail-fast。
- diagnostics 包含 material URI、PBRT source、source contract URI、参数名和失败原因。

### R5: Supported Source Shader Verification

每个 marked-supported source SHALL 有 shader variant 编译与最小渲染验证。

最低覆盖：

- Forward 或 Deferred 中的 realtime source variant。
- OfflineRT direct source variant，若 source 声明支持 OfflineRT。
- final shader reflection 包含 source storage layout。
- 低分辨率 smoke 非全黑或符合 source 的明确数值/图像约束。

### R6: Unsupported Source Gate

暂不支持的 source SHALL 作为负向能力清单存在。

要求：

- unsupported source 在默认 validation profile 下失败。
- 失败不能回退默认材质、debug 色、旧 `MaterialUBO` 或跳过 draw。
- unsupported 列表必须可被 Helmet/BMW/PBRT validation 汇总。

## 测试

### T1: Source Contract Parse And Reflection

解析新增 source contract，断言 source signature、storage layout、参数 schema 和 Material Accessor ABI 可反射。

### T2: PBRT Converter Preservation

导入包含 `glass`、conductor `eta/k`、`mix`、`fourier` 的 PBRT fixture，断言原始参数被保留，supported source 写入正确 `bsdf.source`，unsupported source 输出明确 diagnostic。

### T3: No Silent Approximation

构造不允许近似的高阶材质，断言不能被静默转换成 metallic / matte 成功路径。

### T4: Variant Compile For Supported Sources

对每个 supported source 编译 Forward / Deferred / OfflineRT 声明支持的 variant，断言 final reflection 和 pipeline key 包含 source identity。

### T5: Unsupported Source Negative

在默认 validation profile 下加载暂不支持 source，断言 fail-fast，且 diagnostics 不包含 fallback 成功路径。

### T6: Minimal Visual Or Numeric Validation

对 supported source 运行小场景 smoke 或离线数值验证，断言输出非全黑或满足该 source 的明确约束。

## 修改范围

- `assets/material_sources/` 或等价 source contract 目录
- PBRT converter material mapping
- material source reflection / validation tests
- source-specific shader contract include
- shader variant compile tests
- Helmet/BMW/PBRT validation diagnostics

## 边界与约束

- 不用不可能发生的同 signature layout 冲突写 runtime workaround；若冲突出现，按 bug 诊断。
- 不把 unsupported source 伪装成可渲染成功。
- 不让新增 source 绕过 Material Accessor ABI。
- 不把完整 PBRT 物理模型作为 073-b/e/g 的完成条件。

## 依赖

- `REQ-073-a`: Material v3 source contract、PBRT 参数保留和 accessor ABI。
- `REQ-073-c`: RenderPath material source shader variants and URI migration。
- `REQ-073-e`: realtime material path hard cut and smoke。
- `REQ-073-g`: OfflineRT config hard cut and smoke。

## 后续工作

- `REQ-075-a`: Offline/realtime equivalence 可以选择 supported PBRT source 作为对比对象。

## 实施状态

未实施。
