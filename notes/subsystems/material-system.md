# Material System

> 材质系统的核心不是一堆独立状态，而是 `MaterialTemplate + MaterialInstance` 的分层：template 持有 pass 结构与 shader variants，instance 只持有运行期参数、资源和 pass enable 状态。
>
> 权威 spec: `openspec/specs/material-system/spec.md` + `openspec/specs/forward-shader-variant-contract/spec.md`

## 它解决什么问题

- 把 shader、render state、shader variants、材质参数组织成稳定的运行期对象。
- 避免手写 uniform offset 和 descriptor 绑定表。
- 让材质 pass 结构直接参与 `PipelineKey` 生成。

## 核心对象

- `MaterialTemplate`：定义某个材质有哪些 pass、每个 pass 用什么 shader、variants 和 render state。
- `MaterialInstance`：持有运行期参数，是 `IMaterial` 的唯一实现。
- `RenderPassEntry`：单个 pass 的 shader 配置和 render state。
- `UboByteBufferResource`：把材质内部的 UBO byte buffer 暴露给 backend。

## 典型数据流

1. loader 为每个 pass 决定 shader variants，并编译得到对应 `CompiledShader`。
2. `MaterialTemplate` 持有 pass entries，并把 pass shader 的反射结果并入 template 级 binding cache。
3. `MaterialInstance` 构造时默认启用 template 中全部已定义 pass，并校验这些 pass 的 `MaterialUBO` 布局一致后再分配 byte buffer。
4. 运行时通过 `setVec4` / `setVec3` / `setFloat` / `setInt` / `setTexture` 写参数。
5. `setPassEnabled(pass, enabled)` 只改变 instance 的 enabled subset；对未定义 pass 调用会直接 `FATAL + terminate`。
6. `updateUBO()` 把 dirty 状态传给 `m_uboResource`。

## 关键约束

- shader 里的材质 UBO 名必须是 `MaterialUBO`。
- shader variants 属于 template/pass，不属于 instance；运行时改 UBO 或 texture 不会产生新的 pipeline identity。
- `MaterialInstance` 会断言所有 template-defined passes 的 `MaterialUBO` 布局一致；不一致视为程序错误。
- `setTexture` 绑定的是 `CombinedTextureSampler`，不是裸 texture。
- `getDescriptorResources()` 的顺序固定：先 UBO，再按 `(set << 16 | binding)` 升序排好的纹理资源。
- `getPassFlag()` 是派生视图，不再保存独立真值；它只反映“当前已定义且已启用”的 pass 集合，`UboByteBufferResource` 也会通过回调实时读取这组 flag。
- `getRenderState(pass)` 必须按调用方给的 pass 返回对应 entry 的 render state；pipeline 构建不再偷读默认 Forward entry。
- 当前 engine-wide draw push constant ABI 只有 `model`，lighting / skinning 不再通过 push constant 切接口。
- `loadBlinnPhongMaterial()` 现在只接受固定 forward variant 集：`USE_VERTEX_COLOR`、`USE_UV`、`USE_LIGHTING`、`USE_NORMAL_MAP`、`USE_SKINNING`；非法组合会在 loader 内直接 `FATAL + terminate`。
- 当前 `blinnphong_0` 仍保留 `MaterialUBO.enableAlbedo` / `enableNormal` 两个运行时开关。它们不参与 pipeline identity，但会控制“已声明 sampler 是否真的参与采样”，从而保留“没绑贴图时回退到 `baseColor` / 顶点法线”的旧语义。

## 当前实现边界

- `MaterialTemplate` 仍保留 `RenderPassEntry::bindingCache` 这条旧路径，但运行时主路径主要依赖 template 级 `m_bindingCache`。
- 旧的 `MaterialInstance::create(template, passFlag)` 入参现在只保留兼容外形；当前实现不会用它裁剪初始 enabled pass 集，真正的 truth 是 template 定义 + 后续 `setPassEnabled(...)` 结果。
- 若 enabled passes 没有任何 `MaterialUBO`，实例会回退到 template shader 查找布局。
- `src/infra/material_loader/blinn_phong_material_loader.cpp` 里的 forward loader 会先规范化 variant 子集，再把同一组 enabled variants 同时写进 shader 编译输入和 `RenderPassEntry::shaderSet.variants`。
- 这个 loader 只校验“variant 组合是否合法”，不会提前看 mesh/skeleton；资源层匹配交给 `SceneNode` 在结构校验阶段处理。
- 共享 `MaterialInstance` 的 pass enable 改动属于结构性变化；`Scene` 会调用 `revalidateNodesUsing(materialInstance)` 传播到所有引用它的 `SceneNode`。普通 `setFloat` / `setTexture` / `updateUBO` 不会走这条传播链。

## 从哪里改

- 想加新材质参数：看 shader 反射和 `MaterialInstance` setter 路径。
- 想改 `blinnphong_0` 的变体规则：先看 `src/infra/material_loader/blinn_phong_material_loader.cpp`，再看 `shaders/glsl/blinnphong_0.vert` / `blinnphong_0.frag`。
- 想改 variant 身份：看 `ShaderProgramSet`、loader 和 `RenderPassEntry`。
- 想改 pass enable 对 scene 的影响：看 `MaterialInstance::setPassEnabled()`、`Scene::revalidateNodesUsing(...)` 和 `SceneNode` 的 validated cache。

## 关联文档

- `notes/subsystems/shader-system.md`
- `notes/subsystems/pipeline-identity.md`
- `notes/subsystems/scene.md`
