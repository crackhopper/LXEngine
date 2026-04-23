# Material System

> 材质系统的核心不是一堆独立状态，而是 `MaterialTemplate + MaterialInstance` 的分层：template 持有 pass 结构与 shader variants，instance 只持有一份 canonical 运行期参数、资源和 pass enable 状态。
>
> 权威 spec: `openspec/specs/material-system/spec.md` + `openspec/specs/forward-shader-variant-contract/spec.md`

## 它解决什么问题

- 把 shader、render state、shader variants、材质参数组织成稳定的运行期对象。
- 避免手写 uniform offset 和 descriptor 绑定表。
- 让材质 pass 结构直接参与 `PipelineKey` 生成。

## 核心对象

- `MaterialTemplate`：定义某个材质有哪些 pass、每个 pass 用什么 shader、variants 和 render state。
- `MaterialInstance`：持有运行期参数，是当前唯一的材质类型。
- `MaterialPassDefinition`：单个 pass 的 shader 配置和 render state。
- `MaterialParameterData`：一个完整的材质参数槽位，持有反射布局、CPU 字节和 `IGpuResource` 行为。

## 典型数据流

1. loader 为每个 pass 决定 shader variants，并编译得到对应 `CompiledShader`。
2. `MaterialTemplate` 持有 pass entries，并把 pass shader 的反射结果并入 template 级 binding cache。
3. `MaterialInstance` 构造时默认启用 template 中全部已定义 pass，通过 ownership contract（`isSystemOwnedBinding()`）从 per-pass material-owned binding 列表中收集非系统保留的 buffer bindings（UniformBuffer / StorageBuffer），为每个唯一 binding 名创建一个 canonical `MaterialParameterData`。
4. 运行时通过 `setParameter(bindingName, memberName, value)` 写 canonical 参数（推荐），也可通过 `setFloat` / `setVec3` / `setVec4` / `setInt` 等旧便利 setter 写参数（单 buffer 时自动定位，多 buffer 时 assert）。`setTexture` 仍按 binding 名绑定 canonical 纹理。
5. `setPassEnabled(pass, enabled)` 只改变 instance 的 enabled subset；对未定义 pass 调用会直接 `FATAL + terminate`。
6. `syncGpuData()` 遍历所有参数槽位，把待同步状态传给对应的 `IGpuResource`。

## 关键约束

- 引擎保留的 system-owned binding 名字集：`CameraUBO`、`LightUBO`、`Bones`（定义在 `shader_binding_ownership.hpp`）。非保留名字的 descriptor binding 默认归材质所有。
- 材质 UBO 的名字不再限定为 `MaterialUBO`；任何非系统保留的 `UniformBuffer` binding 都会被自动识别为材质参数缓冲。`MaterialUBO` 仍可用，但不再是特例。
- shader variants 属于 template/pass，不属于 instance；运行时改 UBO 或 texture 不会产生新的 pipeline identity。
- `MaterialInstance` 支持多个 material-owned 参数槽位（UniformBuffer / StorageBuffer）。跨 pass 同名 binding 被视为同一个 canonical binding，布局必须一致，否则 FATAL。
- 首版支持的 material-owned descriptor 类型：`UniformBuffer`、`StorageBuffer`、`Texture2D`、`TextureCube`。不支持的类型在构造期 FATAL。
- `setTexture` 绑定的是 `CombinedTextureSampler`，不是裸 texture。
- `getDescriptorResources(pass)` 是 pass-aware 的：按目标 pass 的反射 bindings 收集材质资源，按 `(set << 16 | binding)` 升序排列。
- `getRenderState(pass)` 必须按调用方给的 pass 返回对应 entry 的 render state；pipeline 构建不再偷读默认 Forward entry。
- 当前 engine-wide draw push constant ABI 只有 `model`，lighting / skinning 不再通过 push constant 切接口。
- `blinnphong_0` 的 variant 依赖规则（如 `USE_NORMAL_MAP` 需要 `USE_LIGHTING + USE_UV`）现在通过 `.material` 文件中的 `variantRules` 声明，由通用 loader 在编译前校验。不合法的 variant 组合直接 `FATAL + terminate`。
- 当前 `blinnphong_0` 仍保留 `MaterialUBO.enableAlbedo` / `enableNormal` 两个运行时开关。它们不参与 pipeline identity，但会控制“已声明 sampler 是否真的参与采样”，从而保留“没绑贴图时回退到 `baseColor` / 顶点法线”的旧语义。

## 当前实现边界

- `MaterialTemplate` 维护 per-pass material-owned binding 列表（`getMaterialBindings(pass)`），跨 pass 按 `findMaterialBinding(id)` 查找。跨 pass 同名 binding 不一致时会直接 FATAL。
- 旧的 `MaterialInstance::create(template, passFlag)` 入参现在只保留兼容外形；当前实现不会用它裁剪初始 enabled pass 集，真正的 truth 是 template 定义 + 后续 `setPassEnabled(...)` 结果。
- variant 依赖校验由通用 loader 根据 `.material` 文件中的 `variantRules` 在编译前执行。不提前看 mesh/skeleton；资源层匹配交给 `SceneNode` 在结构校验阶段处理。
- 共享 `MaterialInstance` 的 pass enable 改动属于结构性变化；`Scene` 会调用 `revalidateNodesUsing(materialInstance)` 传播到所有引用它的 `SceneNode`。普通 `setFloat` / `setTexture` / `syncGpuData` 不会走这条传播链。

## 通用材质资产 (Generic Material Asset)

- `loadGenericMaterial(materialPath)` 读取 `.material` 文件，完成 shader 编译 → 反射 → template 构建 → instance 创建 → 默认参数/资源注入的全流程。
- YAML 格式支持：`shader` 名（全局默认）、全局 `variants`、`variantRules`（variant 依赖校验）、全局 `parameters`（`bindingName.memberName` 格式）、全局 `resources`、per-pass 配置（`shader` 覆盖、`renderState`、`variants`）。每个 pass 可以指定独立的 shader。
- 内置 placeholder textures：`white`、`black`、`normal`，在 `resources` 中直接用名字引用。
- YAML 中的参数/资源名必须在 shader 反射中存在，否则 FATAL。YAML 不参与 ownership 判定。
- Loader 会对全局 YAML 中声明的参数名、member 名、资源 binding 名逐一校验是否存在于材质可见的 shader 反射中，不匹配则 FATAL。
- `passes.<pass>.parameters` 和 `passes.<pass>.resources` 不再支持；材质实例只保存一份 canonical 运行时数据。
- 参考示例：`materials/blinnphong_lit.material`。
## 从哪里改

- 想加新材质类型：写一个 `.material` 并调用 `loadGenericMaterial()`，不需要新的 C++ loader。
- 想加新材质参数：看 shader 反射和 `MaterialInstance` setter 路径。
- 想改 `blinnphong_0` 的变体规则：修改 `materials/blinnphong_lit.material` 或 `blinnphong_default.material` 中的 `variantRules`。
- 想改 variant 身份：看 `ShaderProgramSet`、loader 和 `MaterialPassDefinition`。
- 想改 pass enable 对 scene 的影响：看 `MaterialInstance::setPassEnabled()`、`Scene::revalidateNodesUsing(...)` 和 `SceneNode` 的 validated cache。

## 关联文档

- `notes/subsystems/shader-system.md`
- `notes/subsystems/pipeline-identity.md`
- `notes/subsystems/scene.md`
