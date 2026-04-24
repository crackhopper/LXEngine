# MaterialTemplate：多 pass 蓝图如何收束成统一契约

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/asset/material_template.hpp](../../../../../src/core/asset/material_template.hpp)
出发，关注的不是 API 清单，而是这个类型如何把多个 pass 的 shader、
render state 和 material-owned binding 收束成一份可共享的蓝图。

可以先带着一个问题阅读：为什么 `MaterialTemplate` 既要保留 per-pass 定义，
又要额外构建 canonical material binding 表？答案是，前者回答“这个材质
在结构上有哪些 pass”，后者回答“这些 pass 共同引用的是哪一组稳定 binding 契约”。

源码入口：[material_template.hpp](../../../../src/core/asset/material_template.hpp)

关联源码：

- [material_pass_definition.hpp](../../../../src/core/asset/material_pass_definition.hpp)
- [shader.hpp](../../../../src/core/asset/shader.hpp)

## material_template.hpp

源码位置：[material_template.hpp](../../../../src/core/asset/material_template.hpp)

### MaterialTemplate：多 pass 材质蓝图，而不是运行时数据容器

`MaterialTemplate` 描述的是“一个材质在结构上能做什么”：

- 支持哪些 pass
- 每个 pass 使用哪组 shader / variant / render state
- 每个 pass 暴露了哪些 material-owned binding

它故意不保存参数字节、纹理实例或脏标记；这些运行时状态属于
`MaterialInstance`。这条边界让 template 可以被多个 instance 共享，
并且把 pipeline 身份、反射结构和实例数据拆成三个稳定层次。

### rebuildMaterialInterface：先 canonical 化材质接口，再派生 pass 视图

这个步骤不再把每个 pass 直接存成 `vector<ShaderResourceBinding>` 副本，
而是先建立 template 级的 canonical material binding 表：

- key 是 `StringID(binding.name)`
- value 是该名字对应的唯一 `ShaderResourceBinding` 结构事实

然后每个 pass 只保存“本 pass 用到哪些 canonical binding”的 `StringID` 列表。
这样 template 才真正成为材质结构真值来源：同名 binding 的跨 pass 一致性、
descriptor 类型支持范围、以及运行时实例需要面对的 binding 集合，都在这里一次收束。

### Cross-pass consistency：同名 binding 必须收敛成同一个 canonical 契约

一旦 `MaterialTemplate` 选择“全局 canonical binding 表 + pass 内 StringID 索引”这条路，
它就隐含要求：同名 material-owned binding 在所有 pass 中都必须指向同一份结构契约。

否则每个 pass 只保存 `StringID` 就不够了，因为 runtime 将无法知道：

- 这个名字到底对应哪种 descriptor 类型
- 它的 set/binding 槽位是否稳定
- buffer 的大小和成员布局是否一致

所以现在的校验点不再是“给 instance 的补救检查”，而是 template 在构造自身结构时
就直接 fail-fast。只要 canonical 表建立成功，`MaterialInstance` 就可以放心地
按 bindingName 存 runtime 数据，而不必再重复做结构归并。

## material_pass_definition.hpp

源码位置：[material_pass_definition.hpp](../../../../src/core/asset/material_pass_definition.hpp)

### RenderState：pass 级固定功能状态的可签名快照

`RenderState` 只描述一个 pass 在固定功能阶段上的结构性选择：
剔除、深度测试/写入、比较函数和混合模式。它不关心材质参数值，
也不关心 shader 内部逻辑；它回答的问题是“同一份几何和 shader，
在这一步要用怎样的 raster/depth/blend 规则去提交 pipeline”。

这里同时保留 `getHash()` 和 `getPipelineSignature()` 两条导出路径：

- `getHash()` 面向 C++ 侧缓存键，追求便于组合和快速比较
- `getPipelineSignature()` 面向 `StringID` 组合签名，追求结构可追踪性

也就是说，这个类型不是运行时开关集合，而是 pass 身份的一部分。

### MaterialPassDefinition：把“单个 pass 的结构”收拢成一个对象

`MaterialPassDefinition` 是 `MaterialTemplate` 里的最小结构单元。它把一个 pass
需要稳定共享的三类信息绑在一起：

- `renderState`：固定功能状态
- `shaderProgram`：shader 名称、variant 组合和编译结果

这个类型的重点不是“保存很多字段”，而是给 template 一个明确的 pass 边界。
只要 `MaterialTemplate` 按 pass 持有它，外层代码就能把“Forward/Shadow/... 的结构差异”
收敛成统一接口，而不用分别管理 render state 和 shader 程序选择。

## shader.hpp

源码位置：[shader.hpp](../../../../src/core/asset/shader.hpp)

### StructMemberInfo：把 UBO 成员布局显式带出 shader 反射边界

`StructMemberInfo` 描述的不是“材质参数当前值”，而是 shader 反射出来的
结构布局事实：成员名、类型、偏移和声明尺寸。它存在的意义是让
`ShaderResourceBinding` 不只知道“这是一个 UniformBuffer”，还知道
这个 block 里面有哪些顶层成员、每个成员落在什么偏移。

这一层信息会继续影响材质系统：

- `MaterialTemplate` 会把它们收束进 canonical material binding 表
- `MaterialTemplate` 做跨 pass 一致性检查时，会比较 `members`
- `MaterialInstance` 写参数时，会按成员偏移把值写进 canonical buffer

### ShaderResourceBinding：shader 反射结果在引擎里的统一描述

`ShaderResourceBinding` 是 shader 反射穿过 core/asset 边界之后的统一载体。
它把一个 descriptor binding 需要的结构性信息放到同一个对象里：

- 名字、set、binding：让上层能按语义名或 descriptor 槽位定位资源
- `type` / `descriptorCount`：让上层知道这是什么资源形状
- `size` / `members`：让 buffer 类 binding 保留布局事实，而不是只剩一个名字
- `stageFlags`：保留这个 binding 被哪些 stage 使用

因此，材质系统里提到的“material binding interface”并不是另一种独立格式，
本质上就是把这些 `ShaderResourceBinding` 对象按不同视角重新索引：

- `MaterialTemplate` 做 material-owned 过滤、canonical 化和跨 pass 一致性检查
- `MaterialInstance` 再按 canonical binding id 组织运行时资源

### IShader：把编译产物和反射视图一起交给上层

对材质系统来说，`IShader` 重要的不只是字节码，还包括
`getReflectionBindings()` 这条读路径。`MaterialTemplate::rebuildMaterialInterface()`
依赖它，把 shader 的反射结果
转成材质侧可查询、可校验的 binding 视图。

也就是说，材质模板并不自己理解 GLSL 或 SPIR-V；它只消费 `IShader`
已经整理好的反射结果，并在更高一层决定 ownership、缓存视角和一致性约束。

### ShaderProgramSet：把“哪份 shader 变体参与这个 pass”收束成一项配置

`ShaderProgramSet` 既保存逻辑层的 shader 标识（`shaderName` + variants），
也保存已经解析好的 `IShaderSharedPtr`。这样 `MaterialPassDefinition` 就可以同时回答：

- 这个 pass 的结构签名应该由哪份 shader 名称和哪些 variant 组成
- 运行时如果需要反射 binding，该去拿哪一个 `IShader`

因此它是 pass 结构的一部分，而不是单纯的运行时缓存句柄。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 这个类型真正收束的是两套视图

读这个头文件时，最容易混在一起的是下面两层数据：

- `m_passes`：每个 pass 的完整结构定义，保留 shader、render state 和 pass 本地反射缓存
- `m_passMaterialBindings`：从完整定义里再筛出来的“材质实例可写 binding 视图”

这两层不能互相替代。只有 `m_passes` 才足够表达 pass 身份；
只有 `m_passMaterialBindings` 才足够直接回答 `MaterialInstance`
“我需要为这个 pass 准备哪些材质侧 descriptor resource”。

## 为什么这一页要聚合多个源码文件

如果只看 `material_template.hpp`，读者会很快遇到三个必须跳出的概念：

- pass 本身到底由什么对象收拢
- `binding cache` 里缓存的到底是什么
- shader 反射结果是通过什么接口流进材质模板的

所以这一页故意把 `material_pass_definition.hpp` 和 `shader.hpp` 也并进来。
入口仍然是 `MaterialTemplate`，但支撑它成立的局部概念边界必须一起讲清楚，
否则文档会变成“结论对了，读者仍然得自己回源码补上下文”。

推荐阅读顺序也是反过来的：

1. 先看 `material_template.hpp` 里的整体职责，知道蓝图在收什么
2. 再看 `material_pass_definition.hpp`，理解单个 pass 的结构边界
3. 最后看 `shader.hpp` 里 `ShaderResourceBinding` / `StructMemberInfo`，
   弄清楚 cache 里缓存的不是值，而是反射出来的 binding 结构事实

## 一致性检查为什么放在 template，而不是 instance

跨 pass 同名 binding 的一致性，本质上是蓝图层约束，不是实例层约束。
如果把这件事拖到 `MaterialInstance` 构造或参数写入阶段才发现，
错误语义就会变成“某个实例不能工作”；而这里真正想表达的是
“这份材质模板本身就不是一个自洽的结构定义”。

所以 `checkCrossPassBindingConsistency()` 放在 template 里更合适：
它在共享蓝图形成时就把问题拦下，不让后续实例、scene 校验和 backend
继续建立在一份含糊契约上。

## 和 `MaterialInstance` 的分工

可以把这两个类型的关系记成一句话：

- `MaterialTemplate` 负责声明“允许出现什么”
- `MaterialInstance` 负责记录“这一帧实际填了什么”

这也是为什么 template 要做 ownership 过滤和一致性校验，而 instance
只需要围绕 canonical 参数槽位、纹理表和 pass 级只读筛选来组织运行时数据。

## 推荐的后续阅读

- [MaterialInstance：从模板到运行时账本](material_instance.md)
- [材质系统](../../../../subsystems/material-system.md)
