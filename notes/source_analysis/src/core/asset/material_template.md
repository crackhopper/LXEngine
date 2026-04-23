# MaterialTemplate：多 pass 蓝图如何收束成统一契约

本页的主体内容由 `scripts/extract_source_analysis.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/asset/material_template.hpp](../../../../../src/core/asset/material_template.hpp)
出发，关注的不是 API 清单，而是这个类型如何把多个 pass 的 shader、
render state 和 material-owned binding 收束成一份可共享的蓝图。

可以先带着一个问题阅读：为什么 `MaterialTemplate` 既要保留 per-pass 定义，
又要额外构建一份 `m_passMaterialBindings` 缓存？答案是，前者回答“这个材质
在结构上有哪些 pass”，后者回答“每个 pass 里哪些资源真正归材质实例填写”。

源码入口：[material_template.hpp](../../../../../src/core/asset/material_template.hpp)

## MaterialTemplate：多 pass 材质蓝图，而不是运行时数据容器

`MaterialTemplate` 描述的是“一个材质在结构上能做什么”：

- 支持哪些 pass
- 每个 pass 使用哪组 shader / variant / render state
- 每个 pass 暴露了哪些 material-owned binding

它故意不保存参数字节、纹理实例或脏标记；这些运行时状态属于
`MaterialInstance`。这条边界让 template 可以被多个 instance 共享，
并且把 pipeline 身份、反射结构和实例数据拆成三个稳定层次。

## buildBindingCache：把每个 pass 的反射结果筛成材质可写视图

这个步骤顺着 `m_passes` 遍历每个 `MaterialPassDefinition` 的 shader 反射结果，
但不会把所有 binding 都原样抄下来。它会先用 `isSystemOwnedBinding()` 过滤掉
`CameraUBO` / `LightUBO` / `Bones` 这类由引擎负责供给的 binding，
只把真正归材质实例写入的资源保留在 `m_passMaterialBindings`。

结果仍然按 pass 分组存储。这样 `MaterialInstance` 后续读取时拿到的是
“这个 pass 需要哪些材质侧资源”的局部视图，而不是一个已经抹平作用域的全局表。

## Cross-pass consistency：同名 material binding 必须表达同一份结构契约

template 允许不同 pass 使用不同 shader，但不允许“同名且归材质所有”的 binding
在不同 pass 里偷偷变成不同布局。否则 `MaterialInstance` 就无法继续坚持
“单一 canonical 参数槽位 / 纹理表，被多个 pass 只读复用”这条设计。

因此这里按 binding 名跨 pass 收集首个定义，并在后续出现同名项时做 fail-fast：

- descriptor 类型必须一致
- UBO 大小必须一致
- 反射出的成员布局必须一致

只要任一项冲突，就直接终止，而不是让更晚的参数写入或 descriptor 组装阶段
再出现难以定位的歧义。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 这个类型真正收束的是两套视图

读这个头文件时，最容易混在一起的是下面两层数据：

- `m_passes`：每个 pass 的完整结构定义，保留 shader、render state 和 pass 本地反射缓存
- `m_passMaterialBindings`：从完整定义里再筛出来的“材质实例可写 binding 视图”

这两层不能互相替代。只有 `m_passes` 才足够表达 pass 身份；
只有 `m_passMaterialBindings` 才足够直接回答 `MaterialInstance`
“我需要为这个 pass 准备哪些材质侧 descriptor resource”。

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

- [MaterialPassDefinition：单个材质 pass 的结构边界](material_pass_definition.md)
- [MaterialInstance：从模板到运行时账本](material_instance.md)
- [材质系统](../../../../subsystems/material-system.md)
