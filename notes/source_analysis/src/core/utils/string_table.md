# GlobalStringTable：字符串驻留与结构化身份树

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/utils/string_table.hpp](../../../../../src/core/utils/string_table.hpp)
出发，解释为什么 LXEngine 不只把字符串压成整数，还要支持
`compose / decompose / toDebugString` 这一组结构化身份 API。

可以先带着一个问题阅读：`PipelineKey` 为什么不是一段拼出来的字符串？
答案是，pipeline identity 需要同时满足 hot path 上的整数比较、
cache key 的稳定性，以及调试时能展开 object/material/pass 结构树。

源码入口：[string_table.hpp](../../../../src/core/utils/string_table.hpp)

## TypeTag：让 StringID 不只是“压缩字符串”

`StringID` 最初只是把字符串压成整数，方便比较和做 map key。Pipeline identity
需要的不是单个名字，而是一棵可追踪的结构树：比如
`PipelineKey(MaterialTypeVariant(...), RenderPathNode(...))`。

`TypeTag` 就是这棵树每个节点的类别标签。叶子字符串走 `TypeTag::String`；
`compose(...)` 生成的结构化 ID 会记录自己的 tag 和子字段。这样我们既能保留
整数比较的速度，又能在日志里用 `toDebugString()` 还原出“这个 pipeline key
到底由哪些结构组成”。

## GlobalStringTable：叶子 intern 与结构化 compose 共用一张表

`GlobalStringTable` 同时服务两种需求：

- 叶子名字：`StringID("Forward")`、`StringID("SceneMaterials")`
- 结构身份：`compose(TypeTag::PipelineKey, {materialTypeVariant, renderPathNode})`

这两类 ID 共用同一套整数空间和线程保护，因此上层不需要区分“普通字符串 ID”
和“结构化 ID”的存储方式。区别只存在于可选的 `m_composedEntries` 元数据里：
叶子没有子字段，结构化 ID 可以 `decompose()`，也可以递归 `toDebugString()`。

这对渲染系统很关键：hot path 里仍然只比较 `uint32_t`，而调试 pipeline identity
时又能展开结构树，不必维护一套平行的 debug 字符串。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 补充说明

这里可以继续补充源码之外的上下文；脚本重跑时会保留这一节。
