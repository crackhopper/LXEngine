# MaterialInstance：从模板到运行时账本

本页的主体内容由 `scripts/extract_source_analysis.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页不是重新抄一遍 API，而是顺着
[src/core/asset/material_instance.hpp](../../../../../src/core/asset/material_instance.hpp)
的类型定义来看：这个类为什么要拆成 `MaterialParameterData`
和 `MaterialInstance` 两层，以及为什么它现在坚持
“单一 canonical 参数集 + pass 级只读筛选”这条主线。

可以先带着一个问题阅读：`MaterialTemplate` 已经定义了 pass 和 shader，
为什么还需要一个 `MaterialInstance`？答案是，template 负责“结构是什么”，
instance 负责“这一帧真正要喂给 backend 的实例数据是什么”。

源码入口：[material_instance.hpp](../../../../../src/core/asset/material_instance.hpp)

## MaterialParameterData：一个完整的材质参数槽位

这个类型现在不再只是一个 `IGpuResource` wrapper，而是一个完整的
material-owned buffer binding 运行时对象：

- 持有 `bindingName`
- 持有 `binding`（反射布局）
- 持有 `buffer`（CPU 字节）
- 自己实现 `IGpuResource`
- 自己记录 `dirty`（等待上传）

也就是说，过去 `MaterialBufferSlot + MaterialParameterData` 两层共同表达的概念，
现在收敛成一个类：一个“可写、可查布局、可交给 backend”的材质参数槽位。

这样做的直接收益是，写路径和读路径面对的是同一个对象：

- `setParameter(...)` 直接写 `MaterialParameterData::buffer`
- `getDescriptorResources(pass)` 直接返回这个对象本身
- `syncGpuData()` 直接把这个对象标记为 backend dirty

相比“slot 持有 buffer，wrapper 再引用 slot.buffer”的旧设计，
这里去掉了中间层，也去掉了 non-owning buffer 指针带来的生命周期约束。

## MaterialInstance：模板的运行时账本，而不是第二份模板

如果说 `MaterialTemplate` 像蓝图，`MaterialInstance` 更像一本运行时账本：
它不重新定义 pass 结构，不持有 shader 编译逻辑，也不决定 pipeline 身份；
它负责记录“这次绘制具体要用什么参数、什么纹理、哪些 pass 处于启用状态”。

这个类里最容易看懂的主线有三条：

1. 构造期：从 template 的 per-pass material bindings 收集 slot
2. 写入期：按 binding/member 把值写进 slot buffer，并标记 dirty
3. 读取期：按 pass 视角从 canonical 资源集合里筛选 descriptor resources

所以它本质上是 template 和 backend 之间的一层“实例态翻译层”。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 设计拆解

从职责上看，这几个类型的边界非常清楚：

| 类型 | 解决的问题 | 不负责什么 |
|------|------------|------------|
| `MaterialParameterData` | 把一个 binding 的布局、字节、dirty 状态和 `IGpuResource` 行为绑在一起 | 不决定这个 binding 被哪些 pass 使用 |
| `MaterialInstance` | 管实例态参数、纹理和 pass enable 状态 | 不决定 shader 变体，不生成 pipeline 结构 |

这正是这个设计最重要的一点：`MaterialInstance` 不是“可变的模板”，而是“模板定义之上的运行时数据层”。

## 顺着构造函数看

构造路径可以概括成三步：

1. 先把 template 里定义的 pass 全部放进 `m_enabledPasses`
2. 遍历每个 enabled pass 的 `getMaterialBindings(pass)`，只收 material-owned binding
3. 对每个“跨 pass 同名且布局一致”的 buffer binding 直接创建一个 `MaterialParameterData`

这也是这次收敛的关键：构造期不再需要“两阶段先分配 slot、再创建 wrapper”的技巧，
因为参数字节本体和资源接口已经合并在同一个对象里了。

## 写参数为什么先找 slot，再找 member

这个类没有把 `baseColor`、`roughness` 之类的字段硬编码成成员，而是坚持走：

`bindingName` -> `ShaderResourceBinding::members` -> `offset` -> `memcpy`

这样做有两个直接收益：

- 参数布局由 shader 反射决定，C++ 侧不用再手写 offset
- 同一个 `MaterialInstance` 可以支持多个 material-owned buffer，而不是默认只有一个 `MaterialUBO`

对应地，API 被拆成了两层：

- 推荐路径：`setParameter(bindingName, memberName, value)`，显式指定参数槽位
- 兼容路径：`setFloat(id)` / `setVec4(id)` 这类旧接口，靠成员名全局搜索；如果重名就 assert

这也是为什么 `findSlotByMember()` 会在“同名成员出现在多个 slot”时直接报歧义。

## 为什么现在没有 Per-Pass Override

这次收敛最核心的变化，是把 `MaterialInstance` 明确成“唯一真值来源”：

- `setParameter(...)` 永远写 canonical 参数槽位
- `setTexture(...)` 永远写 canonical 纹理表
- pass 只声明“我使用哪些 binding”，不再持有自己的运行时副本

这样做的收益是：

- 写路径简单，`syncGpuData()` 只需要同步一份数据
- 资源身份稳定，不会因为某个 pass 第一次写值就多出一份影子资源
- 更接近未来 bindless / descriptor 复用需要的“稳定材质资源集合”模型

## `getDescriptorResources(pass)` 是这个类的读路径核心

前面的 setter 和 `syncGpuData()` 更像写路径；真正把实例态数据交给 backend 的入口，是 `getDescriptorResources(pass)`。

它做的不是“把当前所有资源直接返回”，而是“站在某个 pass 的反射视角重新组装一遍 descriptor 列表”：

1. 读取 `m_template->getMaterialBindings(pass)`
2. buffer 类型按 binding 名去 canonical 参数槽位里找
3. texture 类型按 binding 名去 canonical 纹理表里找
4. 最后按 `(set << 16 | binding)` 排序

这意味着 pass 维度的差异只体现在“使用哪些 binding”，而不体现在“同名 binding 持有不同值”。

## `syncGpuData()` 为什么还存在

`MaterialParameterData` 虽然自己实现了 `IGpuResource`，但材质参数写入并没有直接调用基类的 `setDirty()`，
而是先在槽位内部记录 pending-sync，再由 `MaterialInstance::syncGpuData()` 统一转成 backend 可见的 dirty 状态。

这样做的语义是：

- `setParameter(...)` 只负责改 canonical 参数字节
- `syncGpuData()` 才是“把这一轮材质参数写入提交给 backend 同步协议”的显式边界

这也让材质路径和 `CameraData` / `SkeletonData` 这类“写完立刻 `setDirty()`”的资源形成了一个清晰对比：
它们共享同一套 `IGpuResource` 协议，但允许各自选择更合适的脏标记时机。

## 这个设计里最值得警惕的点

目前最需要读源码时记住的，不是 buffer 生命周期，而是反射对象生命周期：

- `MaterialParameterData::binding` 通过原始指针引用 template 里的反射对象

所以当前实现仍然隐含要求：

- `MaterialTemplate` 必须活得比 `MaterialInstance` 更久，或者至少两者共享生命周期

这一点通过 `m_template` 持有 `shared_ptr` 来满足。

## 继续阅读

- [材质系统](../../../../subsystems/material-system.md)
- [MaterialInstance：运行时状态](../../../../concepts/material/material-instance.md)
- [material_instance.cpp](../../../../../src/core/asset/material_instance.cpp)
