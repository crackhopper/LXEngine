# IShader & ShaderResourceBinding：反射结果如何落地到材质系统

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页把反射层当作一条独立的读路径来读，入口是
[src/core/asset/shader.hpp](../../../../../src/core/asset/shader.hpp)。
关注的问题是：shader 反射出来的 binding 列表，是怎么被切开成
“引擎全局数据”和“材质自己管理的资源”这两部分，交给材质系统的。

顺着 `ShaderResourceBinding` → `IShader::getReflectionBindings()` →
`shader_binding_ownership.hpp` 这条线读，可以回答：为什么材质系统不是
直接按 shader 反射结果分配 buffer，而是先经过一道按名字分流的 ownership
过滤，才开始构造 canonical material binding 表。

源码入口：[shader.hpp](../../../../src/core/asset/shader.hpp)

关联源码：

- [shader_binding_ownership.hpp](../../../../src/core/asset/shader_binding_ownership.hpp)

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

## shader_binding_ownership.hpp

源码位置：[shader_binding_ownership.hpp](../../../../src/core/asset/shader_binding_ownership.hpp)

### System-owned binding：按名字切开反射结果的所有权边界

这段代码看起来只是一个常量数组加一个 `contains`，但它其实是反射层和材质层
之间的所有权契约，来源是 REQ-031。

`IShader::getReflectionBindings()` 会把 shader 里所有 descriptor binding 都暴露出来，
不区分谁应该拥有它们。材质系统如果照单全收，就会出现两类 binding 被混在一起：

- 引擎全局数据：相机、光照、骨骼等，这些数据在渲染循环里由引擎统一喂入，
  材质根本不应该有写入权限，也不该为它们分配 buffer
- 材质参数：baseColor、贴图、采样器这些，才是材质 instance 真正管理的资产

`kSystemOwnedBindings` 和 `isSystemOwnedBinding()` 就是这条分隔线的物理实现。
`MaterialTemplate::rebuildMaterialInterface()` 在构造 canonical material binding
表时，会先用这个函数把反射结果里的系统名字过滤掉，剩下的才是 material-owned 的部分。

名字集合故意保持很小、很固定。扩展它意味着“又一个 binding 被声明为引擎全局”，
这是带 spec 变更语义的决定，而不是实现细节。

### getExpectedTypeForSystemBinding：把保留名字钉在固定的 descriptor 类型上

只是把名字列入 `kSystemOwnedBindings` 还不够；引擎必须进一步声明：
这些保留名字对应的 descriptor 类型本身也不能随便换。

如果某一份 shader 意外把 `CameraUBO` 声明成 `StorageBuffer` 或 `Texture2D`，
那就算名字对得上，它表达的语义也已经偏离引擎约定。`getExpectedTypeForSystemBinding()`
用来在这种情况下给上层一个可比对的参照类型，让反射校验能尽早把偏离挡掉，
而不是等到 descriptor set 写入阶段才炸。

非保留名字返回 `std::nullopt`，表示“这一条不归这里管”，上层可以判断是否进入
material-owned 路径而不是误用这个函数做通用类型查询。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 一条阅读主线：反射结果如何变成材质系统能消费的东西

上面两个头文件里的类型，如果只按声明顺序读，其实不太看得出它们放在一起的理由。
真正把它们串起来的是下面这条单向的数据流：

```
SPIR-V 反射
    │
    ▼
StructMemberInfo + ShaderResourceBinding    （shader.hpp）
    │  IShader::getReflectionBindings() 把它们作为一整包暴露
    ▼
isSystemOwnedBinding(name)                   （shader_binding_ownership.hpp）
    │  按名字分流
    ├─► 系统全局路径：CameraUBO / LightUBO / Bones
    │      由引擎循环喂入，材质只读不写
    │
    └─► 材质路径：剩余的所有 binding
           │
           ▼
       MaterialTemplate 构造 canonical material binding 表
           │
           ▼
       MaterialInstance 按 canonical id 管理运行时资源
```

这条线的关键点是“分流发生在 `MaterialTemplate::rebuildMaterialInterface()` 里，
而不是在反射本身”。反射只负责如实报告 shader 里声明了什么；所有权判断、名字保留、
类型校验，都是更高一层做的决定。这也解释了为什么 `shader_binding_ownership.hpp`
物理上是一个小文件，但它承担的责任并不小：它是反射层和材质层之间唯一的、
带明确 spec 语义的边界声明。

## 为什么 `StructMemberInfo` 要走到这一层

仅仅知道一个 binding 是 `UniformBuffer` 并不够。材质系统需要回答的问题是：

- 这个 UBO 里面有哪些材质参数？
- 每个参数落在什么字节偏移上，以便 `MaterialInstance::set("baseColor", ...)`
  知道应该写到 canonical buffer 的哪一段？
- 两份 pass 的 shader 如果共享一个材质语义，对应 UBO 的布局是不是一致的？

这些问题没有办法在 shader 内部解决，必须把 std140 成员布局事实带过核心边界。
所以 `StructMemberInfo` 不是可选的调试信息，而是材质一致性校验和运行时参数写入
这两条路径的共同前提。

## 关于 "material binding interface" 的一个误区

文档里有时候会说到“material binding interface”，听起来像是一种独立的中间格式。
对着源码看，会更清楚：它本质上就是 `ShaderResourceBinding` 序列经过两步加工的视图：

1. 按 `isSystemOwnedBinding(name)` 过滤掉引擎保留项
2. 按 canonical 规则（名字、set/binding、类型、members）收束同义项

`MaterialTemplate` 里所谓 “material-owned canonical binding 表”，就是这两步的结果。
没有额外的数据结构层，只是对反射结果换了一个视角。

## 推荐阅读顺序

- 先读本页 `shader.hpp` 部分的四个 section：建立反射结果的形状认知
- 再读 `shader_binding_ownership.hpp` 两个 section：理解所有权分流这条边界
- 之后跳去 [MaterialTemplate](../material_template.md)：看上述分流和 canonical 化的实际实现
- 最后回到 [MaterialInstance](../material_instance.md)：看 canonical binding id 如何对应到运行时资源
