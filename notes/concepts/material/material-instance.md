# MaterialInstance：运行时状态

如果 `MaterialTemplate` 是菜谱，`MaterialInstance` 就是这一次真正端上桌的菜。它不重新定义 pass，不决定 shader 编译，也不直接创建 pipeline；它只记录“这个实例现在用哪些参数、哪些纹理、哪些 pass 参与渲染”。

## Instance 保存的状态

| 字段 | 当前职责 |
|---|---|
| `m_template` | 指向共享的 `MaterialTemplate` |
| `m_parameterBuffersByName` | 每个 material-owned UBO/SSBO 一个 `ParameterBuffer` |
| `m_textureBindingsByName` | 每个 material-owned texture binding 一个 `CombinedTextureSampler` |
| `m_enabledPasses` | 当前实例启用的 pass 集合 |
| `m_passStateListeners` | pass 启用状态变化时通知外层重建结构缓存 |

instance 构造时会遍历 template 的 canonical material binding 表。只有 `UniformBuffer` 和 `StorageBuffer` 会创建 `ParameterBuffer`；texture binding 只有在 `.material resources` 或代码调用 `setTexture()` 后才有实际资源。

## 参数写入按 binding.member 定位

当前 `.material parameters` 和 editor material override 都使用同一种 key：

```yaml
parameters:
  MaterialUBO.surfaceColor: [0.8, 0.35, 0.25] # -> setParameter("MaterialUBO", "surfaceColor", Vec3f)
  MaterialUBO.mixAmount: 0.35                 # -> setParameter("MaterialUBO", "mixAmount", float)
  MaterialUBO.mode: 0                         # -> setParameter("MaterialUBO", "mode", int)
```

`MaterialInstance` 不靠字符串拼接猜 offset。它先通过 `getParameterBufferLayout(bindingName)` 找到 reflected `ShaderResourceBinding`，再根据 member 的类型和 offset 写入 `ParameterBuffer`。

| API | 当前用途 |
|---|---|
| `setParameter(binding, member, float/int/Vec3/Vec4)` | 写入 UBO/SSBO 成员 |
| `setParameterValue(binding, member, value)` | editor/runtime override 的统一入口 |
| `findParameterMember(binding, member)` | 按 reflection 查询成员是否存在和类型 |
| `readParameterValue(binding, member)` | 复制 instance 数据或 editor 读取当前值 |
| `syncGpuData()` | 把 pending parameter writes 标记为 dirty，等待 backend 上传 |

参数值变化不会改变 pipeline。它只是改变 buffer 字节内容。

## 纹理绑定按 canonical binding 名保存

```yaml
resources:
  albedoMap: white      # -> placeholder texture
  normalMap: normal     # -> placeholder texture
```

loader 会先确认 `albedoMap` / `normalMap` 是 shader reflection 中的 material-owned texture binding，然后调用 `MaterialInstance::setTexture()`。如果值是 `white`、`black`、`normal`，走内置占位纹理；否则按 material 文件所在目录相对路径或运行时路径加载真实图片。

`resources` 不负责 UBO/SSBO，也不负责系统资源。材质 UBO 默认值走 `parameters`，系统 UBO 走 scene/camera/light/skeleton 注入。

## getDescriptorResources(pass) 是 pass-aware 的

一个 instance 可能有多组 canonical binding，但某个 pass 只使用其中一部分。`getDescriptorResources(pass)` 会：

1. 从 template 读取本 pass 的 material binding id 列表。
2. 对 buffer binding 找 `ParameterBuffer`。
3. 对 texture binding 找 `CombinedTextureSampler`。
4. 按 `set/binding` 排序后返回给 scene validation / backend。

缺失 texture 不会在这个函数里直接补齐；真正的缺失检查发生在 `SceneNode::rebuildValidatedCache()`。那里会遍历 shader reflection，确认每个需要的 material-owned resource 都能在 descriptor resources 里找到。

## pass enable 是结构状态

新建 instance 时，template 里定义的所有 pass 默认启用。调用 `setPassEnabled(pass, false)` 会让这个实例跳过该 pass。

这和普通参数不同：

| 改动 | 是否改变 pipeline key | 是否改变 draw 是否存在 |
|---|---|---|
| 改 `MaterialUBO.baseColor` | 否 | 否 |
| 改 `albedoMap` 绑定 | 否 | 否 |
| 关闭 `Forward` pass | 否，key 本身不变 | 是，该 pass 跳过 `RenderingItem` 生成 |
| 换另一个 template/shader | 是 | 可能改变 |

因此 pass enable 变化会通知外层重建 `SceneNode` 的 validated cache。

## Scene 文件里的材质覆盖

`lxe_editor` 加载 scene 时，会先通过 `loadGenericMaterial(uri)` 得到一个新的 `MaterialInstance`，再按顺序应用 material-level override 和 node-level override：

```text
loadGenericMaterial(uri)
  -> apply materialOverrides
  -> apply nodeMaterialOverrides
  -> syncGpuData()
```

override 仍然要经过 reflection 校验：binding/member 必须存在，值类型要能匹配或被允许地转换。

## 我们已经学会了什么

`MaterialInstance` 是运行时账本。它把 template 给出的 canonical binding 接口填上具体值，并按 pass 输出 descriptor resources。它能改变 draw 使用的数据，也能关闭某些 pass，但普通参数和纹理更新不会改变 pipeline identity。

## 下一步

- [多 Pass 材质怎样变成 Draw](pass-rendering-flow.md)
- [模板如何影响 Pipeline](template-and-pipeline.md)
- [MaterialInstance 源码分析](../../source_analysis/src/core/asset/material_instance.md)
