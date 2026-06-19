# MaterialInstance：从模板到运行时账本

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页不是重新抄一遍 API，而是顺着
[src/core/asset/material_instance.hpp](../../../../../src/core/asset/material_instance.hpp)
和
[src/core/asset/parameter_buffer.hpp](../../../../../src/core/asset/parameter_buffer.hpp)
的类型定义来看：这个类为什么要拆成 `ParameterBuffer`
和 `MaterialInstance` 两层，以及为什么它现在坚持
“surface envelope + material source signature + 资源依赖账本”这条主线。

可以先带着一个问题阅读：`MaterialTemplate` 已经定义了 pass 和 shader，
为什么还需要一个 `MaterialInstance`？答案是，material asset 负责描述
“表面是什么”，instance 负责把这份表面合同、资源依赖和运行时状态整理成
scene / render path 可以消费的事实。

源码入口：[material_instance.hpp](../../../../src/core/asset/material_instance.hpp)

关联源码：

- [parameter_buffer.hpp](../../../../src/core/asset/parameter_buffer.hpp)

## material_instance.hpp

源码位置：[material_instance.hpp](../../../../src/core/asset/material_instance.hpp)

### MaterialInstance：surface envelope 的运行时账本

`MaterialInstance` 不再只是 `MaterialTemplate` 的参数副本。071/073 之后，它同时承担
两条边界：

1. surface material 主线：保存 BSDF type、material source URI/signature、contract
   reflection、参数 envelope 和资源依赖。
2. 过渡/非 surface helper：保留 shader-binding buffer/texture API，供 post-process、
   procedural 或旧测试路径使用。

这条分层很重要：envelope-backed surface instance 会忽略旧 shader-binding 参数写入，
防止旧 material UBO 重新成为默认 surface truth。真正会影响 pipeline identity 的是
material type/source variant；普通 envelope 参数值只改变材质数据和上传版本。

## parameter_buffer.hpp

源码位置：[parameter_buffer.hpp](../../../../src/core/asset/parameter_buffer.hpp)

### ParameterBuffer：材质实例里一份可写的 buffer 绑定资源

`ParameterBuffer` 和 `CombinedTextureSampler` 现在是 `MaterialInstance` 持有的两类并列运行时资源。
它不再只是“参数字节数组”，而是一份完整的 buffer-type binding 运行时对象：

- 对应 `MaterialTemplate` canonical material bindings 里的一个 `ShaderResourceBinding`
- 持有 CPU 侧字节数据，供 shader-binding helper 按 member 写入
- 直接实现 `IGpuResource`，让 backend 能按统一资源路径上传

当前它覆盖所有 buffer-type material-owned binding，也就是 `UniformBuffer` 和
`StorageBuffer`；纹理类 binding 则继续由 `CombinedTextureSampler` 表达。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 设计拆解

从职责上看，当前 `MaterialInstance` 有两套有意分开的状态：

| 状态 | 当前用途 | 不负责什么 |
|---|---|---|
| surface envelope / dependency | v2 `.material` 的 BSDF 参数、source signature、资源 URI 与 typed handle | 不决定 pass/shader/render state |
| shader-binding helper | 非 surface 或过渡路径的 buffer/texture 写入 | 不重新成为 surface material 真值 |

这正是硬切后的关键：`MaterialInstance` 可以保留旧 helper API，但 v2 surface material 的真值来自 envelope 和 contract reflection。

## 顺着 v2 路径看

v2 material 的主要构造路径可以概括为：

1. parser 读取 `schema: lxe.material.v2`、`bsdf.type`、`bsdf.source` 和参数 envelope。
2. contract reflection 校验参数名、kind、storage/accessor ABI。
3. `SceneResourceTable` 为 texture/spectrum/bsdf-table 等依赖分配 typed handle。
4. instance 写入 BSDF type、source URI/signature、reflection hash、envelope 和 dependency。
5. `syncGpuData()` 推进 material state version，供后续上传/验证观察。

普通参数值只改变材质数据；`materialTypeVariant` 才会影响 pipeline identity。

## 过渡 helper 的边界

`ParameterBuffer` 仍然把一个 shader binding 的布局、字节和 `IGpuResource` 行为绑在一起。它服务的是非 surface shader-binding helper，例如 post-process、procedural 或专门构造的测试路径。

Envelope-backed surface instance 会忽略旧 shader-binding 参数写入。读这段代码时不要把 `writeShaderBindingParameter(...)` 当作新材质 authoring 表面。

## 继续阅读

- [材质系统总览](../../../../concepts/material/index.md)
- [MaterialInstance：运行时状态](../../../../concepts/material/material-instance.md)
- [material_instance.cpp](../../../../../src/core/asset/material_instance.cpp)

## 继续阅读

- [材质系统总览](../../../../concepts/material/index.md)
- [MaterialInstance：运行时状态](../../../../concepts/material/material-instance.md)
- [material_instance.cpp](../../../../../src/core/asset/material_instance.cpp)
