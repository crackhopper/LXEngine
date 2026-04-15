# Material System

> 材质系统的核心不是一堆独立状态，而是一个可写的 `MaterialInstance`。它依赖 shader 反射结果来分配 UBO、校验类型、暴露 descriptor 资源。
>
> 权威 spec: `openspec/specs/material-system/spec.md`

## 它解决什么问题

- 把 shader、render state、材质参数组织成稳定的运行期对象。
- 避免手写 uniform offset 和 descriptor 绑定表。
- 让材质本身参与 `PipelineKey` 生成。

## 核心对象

- `MaterialTemplate`：定义某个材质有哪些 pass、每个 pass 用什么 shader 和 render state。
- `MaterialInstance`：持有运行期参数，是 `IMaterial` 的唯一实现。
- `RenderPassEntry`：单个 pass 的 shader 配置和 render state。
- `UboByteBufferResource`：把材质内部的 UBO byte buffer 暴露给 backend。

## 典型数据流

1. loader 编译 shader 并做反射。
2. 用反射结果创建 `MaterialTemplate`。
3. `MaterialInstance` 在构造时找到 `MaterialUBO`，分配 byte buffer。
4. 运行时通过 `setVec4` / `setVec3` / `setFloat` / `setInt` / `setTexture` 写参数。
5. `updateUBO()` 把 dirty 状态传给 `m_uboResource`。
6. `RenderQueue::buildFromScene(...)` 把材质资源并入 `RenderingItem`。

## 关键约束

- shader 里的材质 UBO 名必须是 `MaterialUBO`。
- `vec3` 只写 12 字节，不能按 16 字节覆盖相邻 `float`。
- `MaterialInstance` 非拷贝非移动，因为内部 UBO resource 指向自有 buffer。
- `setTexture` 绑定的是 `CombinedTextureSampler`，不是裸 texture。
- `getDescriptorResources()` 的顺序是固定的：先 UBO，再按 `(set << 16 | binding)` 升序排好的纹理资源。

## 当前实现边界

- `MaterialTemplate` 同时保留两套“按名字找 binding”的路径：
  `RenderPassEntry::bindingCache` 以 `std::string` 为 key，
  `MaterialTemplate::m_bindingCache` 以 `StringID` 为 key。
  当前 `MaterialInstance` 的 setter 和纹理绑定主路径实际使用的是后者。
- `RenderPassEntry::shaderSet` 这个过渡结构还在，但当前 `ShaderProgramSet::getShader()` 返回的是 `nullptr`。因此 `entry.buildCache()` 现在基本不会真正填出 per-entry `bindingCache`；运行时依赖的是 template 级别的 `buildBindingCache()`。
- `MaterialInstance::getRenderState()` 现在仍是过渡实现：它不会按传入 pass 取 entry，而是只尝试读取 `Forward` 对应的 `RenderPassEntry`。
- `MaterialInstance::getRenderSignature(pass)` 是按 pass 生效的，`getRenderState()` 目前却还是 Forward-only。这两条路径暂时并不完全对称。
- `MaterialInstance` 构造时只认第一个名字恰好等于 `MaterialUBO` 的 `UniformBuffer` binding；当前还不支持一个材质拥有多个自管 UBO。
- `loadBlinnPhongMaterial()` 当前只配置了一个 `Pass_Forward` entry，并用反射驱动 setter 写入默认值。

## 从哪里改

- 想加新材质参数：先看 shader 反射和 `MaterialInstance` setter 路径。
- 想改 pipeline 行为：看 `RenderPassEntry`、`RenderState`、`getRenderSignature(pass)`。
- 想改默认材质：看 `src/infra/material_loader/`。

## 关联文档

- `openspec/specs/material-system/spec.md`
- `notes/subsystems/shader-system.md`
- `notes/subsystems/pipeline-identity.md`
