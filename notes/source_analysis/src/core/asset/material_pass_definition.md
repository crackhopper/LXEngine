# MaterialPassDefinition：单个材质 pass 的结构边界

本页的主体内容由 `scripts/extract_source_analysis.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/asset/material_pass_definition.hpp](../../../../../src/core/asset/material_pass_definition.hpp)
出发，解释材质系统为什么要先把“单个 pass 的结构”收拢成一个轻量对象，
再由 `MaterialTemplate` 去组织多个 pass。

可以先带着一个问题阅读：为什么这里不直接让 `MaterialTemplate` 平铺保存
render state、shader 和 binding 相关字段？答案是，pass 本身就是材质结构的
一级边界；只有先把这个边界固定下来，后续的模板共享、签名生成和反射缓存
才不会彼此缠在一起。

源码入口：[material_pass_definition.hpp](../../../../../src/core/asset/material_pass_definition.hpp)

## RenderState：pass 级固定功能状态的可签名快照

`RenderState` 只描述一个 pass 在固定功能阶段上的结构性选择：
剔除、深度测试/写入、比较函数和混合模式。它不关心材质参数值，
也不关心 shader 内部逻辑；它回答的问题是“同一份几何和 shader，
在这一步要用怎样的 raster/depth/blend 规则去提交 pipeline”。

这里同时保留 `getHash()` 和 `getRenderSignature()` 两条导出路径：

- `getHash()` 面向 C++ 侧缓存键，追求便于组合和快速比较
- `getRenderSignature()` 面向 `StringID` 组合签名，追求结构可追踪性

也就是说，这个类型不是运行时开关集合，而是 pass 身份的一部分。

## MaterialPassDefinition：把“单个 pass 的结构”收拢成一个对象

`MaterialPassDefinition` 是 `MaterialTemplate` 里的最小结构单元。它把一个 pass
需要稳定共享的三类信息绑在一起：

- `renderState`：固定功能状态
- `shaderSet`：shader 名称、variant 组合和编译结果
- `bindingCache`：从 shader 反射得到的 binding 名称索引

这个类型的重点不是“保存很多字段”，而是给 template 一个明确的 pass 边界。
只要 `MaterialTemplate` 按 pass 持有它，外层代码就能把“Forward/Shadow/... 的结构差异”
收敛成统一接口，而不用分别管理 render state、shader 和反射缓存。

## buildCache：把 shader 反射结果变成按名字可查的 pass 本地索引

`buildCache()` 的角色很窄：它不筛选 ownership，也不做跨 pass 合并；
它只是把当前 pass 的 shader 反射 binding 复制进一个名字索引表，
让调用方可以用 `findBinding(name)` 快速回答“这个 pass 自己声明过什么资源”。

因为 `bindingCache` 是 pass 本地视图，所以这里保留 system-owned 和
material-owned 两类 binding；真正决定“哪些资源归材质实例管理”的步骤，
在更外层的 `MaterialTemplate::buildBindingCache()` 里完成。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 为什么先有 `MaterialPassDefinition`，再有 `MaterialTemplate`

如果直接让 `MaterialTemplate` 自己平铺保存 shader、render state 和反射缓存，
外层代码会很快遇到两个问题：

- 同一个材质有多个 pass 时，字段需要成组复制，边界不清楚
- 想讨论某个 pass 的签名或绑定结构时，没有一个稳定的最小单元可引用

`MaterialPassDefinition` 的价值正是在这里：它让“单个 pass 的结构”先独立成型，
然后再交给 template 组合。这样 `MaterialTemplate` 管的是“多 pass 蓝图”，
而不是一坨彼此并列的配置字段。

## 读这个文件时建议连着看的两个问题

第一，`RenderState::getRenderSignature()` 和 `getHash()` 为什么并存。
前者是结构签名的一部分，强调“这个 pass 从 pipeline 身份角度长什么样”；
后者更像 C++ 侧的缓存辅助值，强调“怎样快速比较和组合”。

第二，`buildCache()` 为什么不做 ownership 判断。因为 ownership 不是单 pass
能独立决定的事；它需要站在 template 角度区分“系统提供的 binding”和
“材质实例负责填写的 binding”。所以这里故意只做 pass 内局部索引。

## 推荐的后续阅读

- [MaterialTemplate：多 pass 蓝图如何收束成统一契约](material_template.md)
- [MaterialInstance：从模板到运行时账本](material_instance.md)
- [材质系统](../../../../subsystems/material-system.md)
