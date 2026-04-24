# MaterialInstance：运行时状态

如果 `MaterialTemplate` 是菜谱，`MaterialInstance` 就是端上桌的那道菜。它是 scene、render queue 和 backend 真正持有的材质对象。

## 它保存了什么

| 字段 | 职责 |
|------|------|
| `m_template` | 指向所属的菜谱 |
| `m_bufferSlots` | `ParameterBuffer` 集合；每个 material-owned UBO/SSBO 一个 canonical buffer binding |
| `m_textures` | canonical 纹理资源 |
| `m_enabledPasses` | 当前启用的 pass 子集 |
| `m_passStateListeners` | pass 开关变化时通知 scene 的回调 |

## 参数槽位是怎样构造出来的

Instance 构造时，从 enabled pass 的 shader 反射里收集所有 material-owned buffer binding：

1. 遍历每个 enabled pass 的 `getMaterialBindings(pass)`
2. 对 `UniformBuffer` 和 `StorageBuffer` 类型的 binding 创建一个 `ParameterBuffer`
3. 每个参数槽位自己持有 byte buffer（零初始化）、dirty 状态，以及 `IGpuResource` 行为
4. 同名 buffer binding 跨 pass 必须布局一致，否则 assert

不支持的 descriptor 类型（如 standalone `Sampler`）会直接 FATAL。

## 写参数：两种方式

**推荐方式**——按 binding 名 + member 名精确定位：

```cpp
mat->setParameter(StringID("MaterialUBO"), StringID("roughness"), 0.5f);
```

**便利方式**——只按 member 名（单 buffer 时自动定位，多 buffer 时 assert）：

```cpp
mat->setFloat(StringID("roughness"), 0.5f);
```

底层都走同一条路径：定位参数槽位 → 查反射 member → 验证类型 → `memcpy` 到 offset。

纹理绑定：

```cpp
mat->setTexture(StringID("albedoMap"), textureSampler);
```

## 为什么没有 Per-Pass 写接口

现在 `MaterialInstance` 明确只保存一份 canonical 参数/资源集合。

- `setParameter(...)` 永远修改这份 canonical 数据
- `setTexture(...)` 永远修改这份 canonical 数据
- pass 只声明“我使用哪些 binding”，不会再持有自己的参数副本

如果确实需要不同 pass 下出现不同值，这不再通过 override 实现，而应该：

- 使用不同 binding 名
- 或拆成不同 `MaterialInstance`

## Pass 开关：为什么是结构变化

```cpp
mat->setPassEnabled(Pass_Shadow, false);
```

pass 开关和普通参数写入不同——它影响的不只是 draw 值，而是：

- 这个对象参加哪些 pass
- SceneNode 需要校验哪些 pass
- descriptor 资源的 pass 维度视图

因此 pass 开关变化会通过 listener 通知 scene，触发引用该材质的节点重验证。`setFloat` / `setTexture` / `syncGpuData()` 不走这条传播链。

## Descriptor 资源的收集

`getDescriptorResources(pass)` 按目标 pass 的反射 bindings 收集材质资源：

1. 枚举该 pass 的 material-owned bindings
2. buffer binding → 从 canonical 参数槽位里按 binding 名取资源
3. 纹理 binding → 从 canonical 纹理表里按 binding 名取资源
4. 按 `(set << 16 | binding)` 排序输出

这意味着不同 pass 看到的是“同一份实例数据的不同使用子集”，而不是各自持有一份独立运行时状态。

## 和 YAML 的对应

```yaml
parameters:                          # → 全局参数槽位默认值
  MaterialUBO.baseColor: [0.8, 0.8, 0.8]
  MaterialUBO.shininess: 12.0

resources:                           # → 全局默认纹理
  albedoMap: white

passes:
  Forward:
    renderState:
      depthTest: true
```

## 继续阅读

- 代码入口：[material_instance.hpp](../../../src/core/asset/material_instance.hpp)
- 实现层设计：[../../subsystems/material-system.md](../../subsystems/material-system.md)
