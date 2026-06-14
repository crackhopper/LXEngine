# REQ-071-a: SurfaceMaterial v2 PBRT Pure Envelope Contract

> 2026-06-10 新增：本 REQ 是 `REQ-071` 连续需求族的第一步。目标是把材质参数层收敛到 PBRT surface material 语义，并明确 `SurfaceMaterial` / `SurfaceMaterialTemplate` / `MaterialInstance` / `SurfaceMaterialResourceParser` 的边界，为后续 render path graph、render feature、resource table、GPU cache、scene package 和离线/实时对齐测试提供稳定合同。

## 背景

BMW M6 验证暴露出当前材质系统的两个根本问题：

| 问题 | 影响 |
|---|---|
| 材质参数过度简化为 PBR 近似 | `glass`、`metal eta/k`、`fourier`、`substrate`、`matte sigma` 等 PBRT 语义丢失，后续离线 reference 无法准确复现 |
| `MaterialTemplate` 同时承担参数 contract 与 technique/pass 结构 | 代码和文档里容易把“BSDF 类型”与“Forward/Deferred/OfflineRT 渲染框架”混在一起，导致 shader、pass、参数和 pipeline 身份边界不清 |

我们已经在 `notes/concepts/material/material-contract-v2.md` 讨论过目标模型：以 PBRT surface material 为主语义，`SurfaceMaterialTemplate` 对应 PBRT `type` 背后的公式和参数 schema；`MaterialInstance` 保存某份材质文件或 scene override 后的具体参数和资源 handle。材质文件必须是 pure envelope：它只描述 BSDF/表面参数和资源引用，不描述 shader、pass、render path 或 render state。

本 REQ 只处理材质参数层和 parser 边界，不实现 `RenderPathGraph` pass DAG、`RenderFeature`、GPU resource table 或 scene package。这些在 `REQ-071-b` 之后继续推进。

## 071 需求族总览

| REQ | 主题 | 解锁关系 |
|---|---|---|
| `REQ-071-a` | SurfaceMaterial v2 PBRT pure envelope contract | 定义 BSDF 参数和 parser 资源注册 |
| `REQ-071-b` | RenderPathGraph / RenderPassNode / RenderFeature contract | 让 render path graph 显式声明 pass、shader、依赖和 frame graph 资源 |
| `REQ-071-c` | SceneResourceTable parser / manager / resource abstraction | 把 material、mesh、texture、camera、light 等解析拆出，并用 URI 管资源身份 |
| `REQ-071-d` | GPUResourceTable、RHI cache 与异步 upload task | 把 backend resource / pipeline cache / upload 统一到平台无关接口 |
| `REQ-071-e` | Scene package 与快速加载 | 序列化 scene resource table 解析结果，减少重复解析和加载卡顿 |
| `REQ-071-f` | Helmet / BMW offline-realtime 对齐验收 | 用低分辨率直接光照、Forward/Deferred/OfflineRT 对比验证整条链路 |

## 目标

1. `.material` v2 以 PBRT `SurfaceMaterial` pure envelope 为主模型，覆盖 BMW M6 当前实际出现的材质类型。
2. `SurfaceMaterialTemplate` 只表达 BSDF type 的参数 schema、默认布局和校验规则，不保存 render path / pass / shader。
3. `MaterialInstance` 保存具体参数值、资源 handle、template 引用、render class / tag，不保存 pass graph 或 shader binding。
4. 新增 `SurfaceMaterialResourceParser`，负责解析材质文件、校验参数 envelope、注册材质依赖资源，但不拥有资源。
5. 前置 `MaterialResourceParser` 必需的最小 `SceneResourceTable` URI 解析、加载、去重和 dependency 注册接口。
6. PBRT converter 负责把默认值显式写进 material 文件；运行时 parser 不隐式补默认值。
7. 清理旧 runtime PBR 参数模型和旧 material loader 逻辑；新代码基于 PBRT BSDF 参数结构重写。
8. 材质 envelope 不直接绑定 shader；后续 `RenderPathGraph` 通过 render class、BSDF type 和 reflection 校验 shader 所需 material-owned 数据。

## 命名与边界

| 名称 | 定义 |
|---|---|
| `SurfaceMaterial` | 应用于物体表面的 pure envelope，只包含 BSDF/表面参数、资源引用、render class / tag 和 authoring metadata |
| `SurfaceMaterialTemplate` | 某个 PBRT BSDF type 的 schema、参数校验和布局规则 |
| `MaterialInstance` | 某份 `SurfaceMaterial` 或 scene override 后的运行时参数对象，持有 typed resource handles 和 dirty/version |
| `RenderClass` | 用于 render path graph 过滤和归类的标签，例如 `surface.opaque`、`surface.transparent`、`surface.alpha-test` |
| `RenderPath` | Forward、Deferred、OfflineRT 等顶层渲染框架，不写入 `.material` |
| `RenderPathGraph` | 某个 RenderPath 的 pass DAG、shader、source/target、render state 和 feature 依赖，由 `REQ-071-b` 定义 |
| `RenderFeature` | shadowmap、SSAO、GI、bloom、tone mapping 等算法/效果的参数 envelope，由 `REQ-071-b` 定义 |

`SurfaceMaterial` SHALL NOT 包含 `defaultTechnique`、`techniques`、`passes`、`shader`、`renderState`、`targets`、`sources`、`variants`、`variantRules` 或旧 root `parameters` / `resources` 字段。需要表达透明、alpha test、double-sided 等分类时，使用 `renderClass` / tag / BSDF 参数，而不是把 pass 或 shader 塞回 material。

## 需求

### R1: PBRT Surface Material 类型覆盖

Material v2 SHALL 覆盖 BMW M6 用到的 PBRT surface material 类型：

| PBRT type | Runtime class / template intent | 必须保留的语义 |
|---|---|---|
| `matte` | `MatteSurfaceMaterial` / `MatteMaterialTemplate` | `Kd`、`sigma`；`sigma > 0` 对应 Oren-Nayar |
| `glass` | `GlassSurfaceMaterial` / `GlassMaterialTemplate` | `Kr`、`Kt`、`eta`、roughness、transparent dielectric intent |
| `uber` | `UberSurfaceMaterial` / `UberMaterialTemplate` | diffuse、specular、optional transmission、opacity、eta |
| `metal` | `MetalSurfaceMaterial` / `MetalMaterialTemplate` | conductor `eta` 和 `k`，支持 SPD/resource 引用 |
| `substrate` | `SubstrateSurfaceMaterial` / `SubstrateMaterialTemplate` | diffuse substrate + glossy lobe，`Kd`、`Ks`、`uroughness/vroughness` |
| `fourier` | `FourierSurfaceMaterial` / `FourierMaterialTemplate` | `.bsdf` 表引用，不降级覆盖原始语义 |
| `mix` | `MixSurfaceMaterial` / `MixMaterialTemplate` | 两个 named material 引用与 `amount` |

当前 shader 不支持的 BSDF 也 SHALL 被 parser 和 resource table 完整保存；渲染路径可以在 technique 校验阶段报 unsupported，而不是在材质解析阶段丢字段。

### R2: 参数 Envelope 为唯一文件表达

`.material` v2 中的 BSDF 参数 SHALL 使用统一 envelope，不允许裸 YAML 值。

最低字段：

| 字段 | 说明 |
|---|---|
| `kind` | `float`、`rgb`、`spectrum`、`bool`、`string`、`texture`、`integer`、`materialRef`、`bsdfTable` |
| `value` | 内联常量 |
| `uri` | texture、SPD、BSDF table、material reference 等资源 URI |
| `valueType` | texture / buffer 参数的逻辑值类型，例如 `rgb` 或 `float` |

规则：

- `value` 与 `uri` 互斥，除非字段 contract 明确允许两者共同出现。
- `texture`、`spectrum`、`bsdfTable` 参数通过 `uri` 注册依赖资源。
- 参数名 SHALL 保持 PBRT 语义名；同一个参数名可以承载常量或 resource 引用。例如 `Kd` 可以是 `{ kind: rgb, value: [...] }`，也可以是 `{ kind: texture, valueType: rgb, uri: ... }`。不得为 texture 另造 `KdTexture`、`baseColorTexture` 这类并行参数来绕开 PBRT schema。
- `mix.namedmaterial1` / `mix.namedmaterial2` 使用 material reference envelope，不使用普通字符串偷渡；material reference 使用 `uri` 字段保存目标 `.material` 文件地址。
- `MaterialResourceParser` 对 material reference SHALL 至少读取目标 material 的 header / metadata，确认其 `bsdf.type`。如果 `mix.namedmaterial1` 或 `mix.namedmaterial2` 指向的目标也是 `bsdf.type: mix`，解析 SHALL fatal。第一阶段禁止 multi-level mix，避免递归材质树和逐层混合带来的实现与性能复杂度。
- `MaterialResourceParser` 不递归展开 material reference 的完整参数、texture、technique，也不在解析阶段做 material reference 去重加载。被引用 material 的完整加载和去重由后续 `SceneResourceTable` 资源管理阶段处理。
- `mix.amount` 第一阶段只允许 `kind: float` 常量，不允许 `kind: texture`。这个限制只针对 mix mask；leaf material 的 `Kd`、`Ks`、`Kt`、roughness 等 PBRT 参数仍然允许 texture/resource 引用。
- scene override 只能覆盖参数 envelope 的 value/resource handle，不修改 template schema。
- 材质文件不记录参数来源字段。`explicit`、`pbrt-default`、`converted` 等来源信息属于 converter diagnostics / manifest，不进入运行时 `.material` contract，后续系统也不依赖这些字段。

### R2.1: Material Parser 所需的最小 SceneResourceTable 接口

本 REQ SHALL 前置 `MaterialResourceParser` 解析材质依赖所必需的最小 `SceneResourceTable` 能力。不能让 parser 只保存裸 URI 字符串再等待后续需求补齐资源语义。

最低接口能力：

| 能力 | 说明 |
|---|---|
| `resolveUri(baseUri, uri)` | 将 material-relative URI 解析为 canonical URI |
| `loadOrGetResource(type, uri)` | 按资源类型和 canonical URI 加载或返回已有 handle |
| `registerDependency(ownerHandle, dependencyHandle)` | 记录 material instance 到 texture/spectrum/bsdfTable/materialRef 等依赖 |
| typed handle | 返回 `TextureHandle`、`SpectrumHandle`、`BsdfTableHandle`、`MaterialHeaderHandle` 等类型化 handle |
| diagnostics | 资源加载失败时带 material URI、参数路径、resource URI 和 parser 名称 |

去重规则：

- 同一 canonical URI + resource type SHALL 只加载一次。
- `kind: texture`、`kind: spectrum`、`kind: bsdfTable` SHALL 在 material parse 阶段立即解析、加载、去重，并把 typed handle 写入 `MaterialInstance`。
- `kind: materialRef` SHALL 至少加载目标 material header / metadata，确认不是 multi-level mix，并注册 dependency；完整 material instance 的加载可由 resource table 调度，但不能把引用保持为未校验裸字符串。
- resource table 是资源 owner；parser 只拿 handle。

`REQ-071-c` 继续扩展这套接口到 mesh、camera、light、effect、完整 dependency graph、reload dirty 和 package-ready graph，但不再作为这套最小接口的首次定义点。

### R3: Converter 默认值显式化

PBRT converter SHALL 支持独立默认值配置文件，例如 `pbrt-defaults.yaml`。

转换规则：

| 情况 | 行为 |
|---|---|
| PBRT 源材质显式提供参数 | 使用源参数，并在 converter report 中记录为 explicit |
| 源材质缺参数，默认配置提供参数 | 使用配置值，打印 warning，并在 converter report 中记录为 pbrt-default |
| 源材质和默认配置都缺参数 | converter fatal |

默认值 SHALL NOT 硬编码到 converter 代码。运行时 `MaterialResourceParser` SHALL NOT 为缺失参数补默认值；缺失 required 参数时 fail-fast。

### R4: SurfaceMaterialTemplate 只表达 BSDF 参数合同

`SurfaceMaterialTemplate` SHALL 调整为 PBRT type / BSDF type 的参数 schema 和数据布局对象。现有代码如果仍使用 `MaterialTemplate` 类名，语义上必须收敛为 `SurfaceMaterialTemplate`；不得继续把 render path / pass / shader 结构挂在 template 上。

第一阶段 `SurfaceMaterialTemplate` SHALL 只由 `bsdf.type` 决定，不再按具体用途、材质名或外观风格拆更细 template。也就是说，所有 `substrate` 材质共享同一个 `SubstrateMaterialTemplate`，所有 `glass` 材质共享同一个 `GlassMaterialTemplate`。`CarPaint`、`Rubber`、`LeatherBlack` 等差异属于 `MaterialInstance` 的参数值、资源引用或 `RenderClass` / tag，不属于新的 template type。

它 SHALL 负责：

- required / optional 参数 schema。
- 参数 envelope 类型校验。
- 参数到 CPU/GPU material data layout 的映射规则。
- BSDF type id / debug name。
- BSDF type id / render class 建议值。

它 SHALL NOT 负责：

- render path 列表。
- pass 列表。
- shader URI。
- source/target。
- render state。
- pipeline signature 中的 pass/shader 结构部分。

### R5: MaterialInstance 保存参数与资源 handle

`MaterialInstance` SHALL 保存：

- `SurfaceMaterialTemplate` 引用。
- 具体参数 envelope 表，作为唯一运行时参数真相。
- 通过 `SceneResourceTable` 分配的 texture/spectrum/bsdf-table/material reference handle。
- `RenderClass` / tag / authoring metadata。
- scene override 后的运行时参数状态。
- 参数 dirty/version 标记，用于通知后续 upload 或 shader-facing 数据重建。

`MaterialInstance` SHALL NOT 修改 template，也 SHALL NOT 修改 `RenderPathGraph` / pass / shader 定义。scene/node override 只能创建新的 `MaterialInstance` identity 或修改该对象的 instance-level override 数据。这里的“创建新 instance”指生成另一个运行时对象实例，不是 C++ 类继承。

`MaterialInstance` SHALL NOT 在自身内部维护第二套 derived material parameter record。GPU upload 所需数据应在上传阶段从 envelope 参数表读取并生成；参数或资源 handle 改变时只需要更新 dirty/version。这样避免 authoring 参数表和 GPU 参数表在同一个 instance 内形成双轨真相。

### R6: SurfaceMaterialResourceParser 拆分材质解析逻辑

新增 `SurfaceMaterialResourceParser`，从当前 material loader / scene resource table 中拆出材质文件解析逻辑。现有 `MaterialResourceParser` 文件名可以作为过渡，但职责必须是解析 `SurfaceMaterial` pure envelope。

它 SHALL：

- 读取 `.material` v2 文件。
- 校验 schema、BSDF type、参数 envelope 和 required 参数。
- 创建或查找对应 `SurfaceMaterialTemplate`。
- 创建 `MaterialInstance`，并写入参数 envelope、typed resource handles、render class / tag。
- 调用 `SceneResourceTable` 的最小资源接口解析、加载、去重并注册 texture、spectrum、bsdf-table、material header 等依赖。
- 对 `materialRef` 保存 URI 字段和参数关系，读取目标 material header / metadata 以禁止 multi-level mix，但不递归解析或去重加载被引用 material 的完整资源。

它 SHALL NOT：

- 持有资源生命周期。
- 直接创建 GPU descriptor 或 Vulkan object。
- 对缺失字段做隐式兜底。
- 根据文件名、pass 名或 BSDF type 推导 shader。
- 解析 `defaultTechnique`、`techniques`、`passes`、`renderState`、`variants` 或旧 root `parameters` / `resources`。

### R7: 清理旧 Runtime PBR 参数模型

Material v2 SHALL 替换旧 runtime PBR 参数模型。旧的 `baseColor / metallic / roughness` 作为顶层材质真相的代码路径、loader 逻辑和隐式兼容 SHALL 被清理，不作为兼容层保留。

规则：

- `.material` 运行时合同以 PBRT BSDF 参数 envelope 为唯一材质参数入口。
- 旧 PBR 参数文件格式不需要兼容；迁移后的 material 必须显式使用 `bsdf.type` 和 PBRT 风格参数。
- shader 只能从 PBRT BSDF 参数按当前 technique 的公式读取/计算材质数据，不能读取另一套旧 PBR 参数模型。
- 旧材质定义模式不兼容、不迁就；相关 parser、loader、runtime parameter、shader binding 代码路径都应删除或重写到 PBRT BSDF contract 上。
- 旧 material loader 中围绕 `baseColorFactor`、`metallicFactor`、`roughnessFactor` 等旧合同的逻辑应删除或迁移到 PBRT BSDF parser/technique 支持中。

### R8: SurfaceMaterial 与 RenderPathGraph 的 Reflection 边界

Material v2 SHALL 继续利用现有 shader reflection 能力，而不是为每个 shader、每个 material type 或每个用户扩展 shader 生成临时 C++ 类。但 reflection 校验发生在 `RenderPathGraph` / `RenderPassNode` 绑定 shader 时，材质文件自身不保存 shader 或 pass。

规则：

- `MaterialInstance` 的唯一参数真相仍然是 envelope 参数表。
- `RenderPathGraph` 中的 pass shader 编译后，通过 reflection 得到 material-owned buffer、texture、image、sampler 和字段 layout。
- `RenderPathGraph` validation SHALL 用 reflection 校验匹配到该 pass 的 `SurfaceMaterial` envelope 是否满足 shader 对 material-owned binding 的需求。
- 缺失 required material-owned binding 或字段 SHALL 使该 material 在对应 `RenderPath` / pass 下不可用；错误必须包含 material URI、render path、pass、shader 和字段路径。
- 系统固定 ABI 变量不走 material-owned reflection 表；它们在 `REQ-071-b` 中由固定 C++/GLSL common contract 管理。
- 用户自定义 shader 第一阶段走 generic reflected parameter block；不生成临时 C++ 类，也不注册运行时 C++ 类型。

normal map 属于材质参数/资源的一种常用能力，Material v2 SHALL 支持表达 normal map texture。PBRT BMW M6 当前可能不使用 normal map，但材质 contract 不应因此排除它。

### R9: 同 SurfaceMaterialTemplate 多 MaterialInstance 的批量绘制前提

同一个 `SurfaceMaterialTemplate` 的多个 `MaterialInstance` SHALL 共享同一套 BSDF parameter schema。Forward / Deferred / OfflineRT 由 `RenderPathGraph` 选择对应 pass/shader，并通过 object/material index 访问不同 instance 的参数。

要求：

- 多个 instance 不应导致不同 C++ 参数类。
- 渲染层可以把同一 render path / pass / shader / render class 下的 object 组织到同一 pipeline。
- instance 参数差异通过 material index 访问全局 material data / reflected parameter storage。
- 是否合并 mesh 或使用 indirect draw 不属于 `071-a`，但 `071-a` 必须保证 material data 可以被数组化上传。

## 测试

### T1: Material v2 Parser Contract

新增 parser 测试：

- 每个 PBRT type 的最小合法 `.material` 能解析。
- 缺 required 参数 fatal。
- 参数 envelope kind/value/uri 类型错误 fatal。

### T2: Converter Defaults

新增 converter 默认值测试：

- 源文件缺参数且默认配置存在时，输出显式参数并记录 warning。
- 源文件和默认配置都缺参数时 fatal。
- 输出文件不依赖 converter 代码内默认常量。
- converter report/manifest 记录哪些参数来自 PBRT explicit 值，哪些来自默认配置；运行时 `.material` 文件不包含这些来源字段。

### T3: Resource Dependency Registration

构造含 texture、SPD、BSDF table、mix material ref 的材质，验证：

- `MaterialResourceParser` 通过 `SceneResourceTable` 注册依赖。
- 相同 URI 不重复加载。
- instance 保存 handle，不保存 parser 私有资源对象。

### T4: SurfaceMaterial / RenderPathGraph Boundary

验证 `SurfaceMaterialTemplate`、`.material` v2 和 `MaterialInstance` 中不存在 shader/pass/render state/defaultTechnique/techniques；这些字段只出现在后续 `REQ-071-b` 的 `RenderPathGraph` contract 中。

### T5: Reflection-driven Material Parameters

构造一个自定义 material shader：

- shader 声明 material-owned buffer 和 texture。
- `.material` 提供对应 envelope 参数和 texture URI，但不提供 shader/pass。
- `RenderPathGraph` pass 绑定该 shader，并通过 reflection 校验字段存在、类型匹配。
- 缺字段时该 render path graph validation 失败。
- 不生成任何 shader-specific C++ material class。

### T6: Helmet Material v2 Migration Smoke

本 REQ 完成时 SHALL 把 helmet smoke scene 迁移到 Material v2，并用新的 PBRT BSDF material contract 渲染。

要求：

- helmet scene 不再依赖旧 runtime PBR material 文件格式。
- helmet 所有可见材质都使用 `schema: lxe.material.v2`、`bsdf.type` 和 envelope 参数。
- 如果 glTF 原始材质只能近似映射到 PBRT BSDF type，转换报告必须说明映射关系；运行时 material 文件仍只能保存新的 PBRT BSDF contract。
- editor 加载 helmet scene 不 fatal。
- realtime viewport 至少能渲染出非全黑图像。
- offline smoke 以低分辨率直接光照 profile 渲染 helmet，输出非全黑图像。
- editor/offline smoke 都必须覆盖 material parser、resource table dependency、shader reflection validation 和 material upload 基础路径。

### T7: Per-REQ Rendering Smoke Gate

从 `REQ-071-a` 开始，071 族每个 REQ 完成时都 SHALL 运行同一组基础渲染 smoke：

| Smoke | 目的 |
|---|---|
| editor helmet load/render | 证明 editor/realtime 渲染仍可用 |
| offline helmet direct render | 证明 offline 路径仍可用 |
| material validation diagnostics | 证明缺材质字段、缺资源或 unsupported shader 不会静默黑屏 |

如果某个后续 REQ 还没实现完整目标，但当前 REQ 为了保持 smoke 通过引入了临时代码，必须：

- 在代码附近用明确 TODO/diagnostic 标记临时桥接。
- 在当前 REQ 的实施状态中记录临时桥接原因。
- 在对应未来 REQ 的“后续重构/迁移事项”中记录需要删除或重写的点。
- 临时代码不得伪装成最终 contract，也不得恢复旧材质定义兼容路径。

## 修改范围

- `src/core/asset/`：SurfaceMaterialTemplate / 现有 MaterialTemplate 收敛、MaterialInstance、参数 schema/envelope 类型。
- `src/infra/material_loader/` 或新目录：MaterialResourceParser。
- `src/infra/offline/` / converter 相关代码：PBRT 默认值配置读取和 material v2 输出。
- `assets/materials/`：新增 material v2 样例。
- `notes/concepts/material/`：同步概念页。
- `src/test/`：parser、converter、resource dependency 测试。

## 边界与约束

- 本 REQ 不实现 RenderPathGraph pass DAG；只为 `REQ-071-b` 提供数据边界。
- 本 REQ 不实现玻璃折射、Fourier BSDF、spectral metal shader，只保证数据不丢。
- `fourier` 第一阶段只要求 parser 加载/去重 `.bsdf` resource，并在 `FourierSurfaceMaterial` / `MaterialInstance` 中保存 `bsdffile` handle；不要求 shader 真实 evaluate Fourier BSDF table。当前 technique 不支持 Fourier 时必须输出明确 unsupported diagnostic；真实 Fourier evaluation 后续单独 REQ。
- 本 REQ 不保留旧 `.material` 格式兼容层；需要继续使用的资产必须迁移到 v2。
- 不保留旧 `passes:` 顶层格式的兼容逻辑；迁移后的 material 必须显式使用 v2 contract。
- 代码中不得根据 BSDF type 隐式选择 shader。
- `.material` v2 不得包含 render path / technique / pass / shader / render state；所有 shader 绑定由 `REQ-071-b` 的 `RenderPathGraph` 负责。
- 不为用户自定义 shader 自动生成 C++ 类；用户 shader 的 material-owned 数据由 reflection + generic parameter storage 承载。
- 不保留旧 runtime PBR 参数模型作为并行数据路径。
- 不保留旧材质定义模式的兼容 parser 或兼容 runtime path。

## 依赖

- `REQ-067-a`：SceneResourceTable 作为资源 owner 和 handle 分配入口。
- `REQ-070-a`：BMW M6 PBRT 转换工具提供真实 PBRT 输入语义。
- `notes/concepts/material/material-contract-v2.md`：当前设计草案。

## 后续工作

- `REQ-071-b`：定义 RenderPath、RenderPathGraph、RenderPassNode、RenderFeature 和 FrameGraph source/target contract。
- `REQ-071-c`：把 071-a 中前置的最小资源接口扩展到 mesh、texture、camera、light、effect 等全场景资源，并清理 SceneResourceTable/parser 文件边界。
- `REQ-071-f`：用 helmet/BMW 验证 material v2 在 realtime/offline 的基础等价。
- 后续每个 071 REQ 都必须继承 T7 的 helmet editor/offline smoke gate，并清理前序 REQ 记录的临时桥接。

## 实施状态

2026-06-14 复核关闭：071-a 的 Material v2 pure envelope、PBRT 参数保留、旧 root material 字段拒绝、material dependency registration 和 PBRT converter 输出已经落地；后续 Material v3 的 `bsdf.source`、source reflection hash、storage ABI 和 shader variant 边界已由 `REQ-073-a` 之后的需求接管。

本文件不再作为 active 实施单元保留。后续不得回退到 material-local technique/pass/shader 合同；当前正向事实以 Material v3/source contract 和 RenderPathGraph 需求为准。

### 2026-06-11: 默认/runtime 旧材质真相已关闭，pure envelope 边界重新确认

- 默认 PBR runtime 资产 `assets/materials/pbr.material` 与 `assets/materials/pbr_gold.material` 已改为纯 `lxe.material.v2` PBRT envelope，不再包含 root `parameters:` / `resources:`、`MaterialUBO.*`、旧 PBR 参数名或 material 内嵌 shader/variant/technique 字段。
- PBRT converter 输出 BMW runtime material v2 文件时只写 `schema` 与 `bsdf.parameters` envelope；`mix` 的 `materialRef` 已写成真实生成的 `.material` URI，不再使用 `named:` 占位。
- `GenericMaterialLoader` 对所有 `schema: lxe.material.v2` 文件直接走 `MaterialResourceParser` 的完整 YAML root 解析；v2 文件里的旧 root render-flow / parameters / resources 字段由 parser fatal，不再落入旧 loader root 参数路径。
- GPU material record 生成从 PBRT envelope 读取默认材质数据，不再从 `baseColorFactor`、`metallicFactor`、`roughnessFactor`、`ao` 旧 PBR 名称回退。
- GLTF 默认材质加载将 base color / normal texture 写入 PBRT envelope 参数 `Kd` / `normalmap`，并由 `SceneResourceTable` 映射到 bindless `baseColorTexture` / `normalTexture` upload slot；v2 路径不再绑定旧 `albedoMap` / `normalMap`。
- 当前仍保留 root `MaterialUBO.*` 的资产是 BlinnPhong/RTR/debug 等显式 custom legacy shader material，不属于默认 PBRT/PBR runtime 路径；这部分继续留给 `REQ-071-b` 的 RenderPathGraph / shader-binding 边界清理。
- 已确认后续不得把 `defaultTechnique`、`techniques`、pass、shader、renderState 重新放回 `SurfaceMaterial`；Forward、Deferred、OfflineRT 的差异必须由 `RenderPathGraph` 表达。
- 本轮验证通过：`ninja -C build test_gltf_scene_asset_loader test_scene_resource_table test_generic_material_loader test_scene_runtime test_material_instance test_material_v2_parser test_material_v2_resource_dependencies`、`ctest --test-dir build --output-on-failure -R 'test_(material_instance|material_v2_parser|material_v2_resource_dependencies|scene_resource_table|generic_material_loader|scene_runtime|gltf_scene_asset_loader)'`、`python3 src/test/integration/test_lxe_pbrt_scene_convert.py --source-dir /home/lixiang/proj/LXEngine`，以及针对 `pbr.material`、`pbr_gold.material`、BMW runtime material 的旧字段 grep 审计。
