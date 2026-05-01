# REQ-006: GlobalStringTable 扩展 — TypeTag / Intern / compose / decompose

## 背景

当前 `src/core/utils/string_table.hpp` 只做"字符串 ↔ uint32 ID"双向映射，通过 `StringID` 隐式构造暴露给调用方。这对**叶子字符串**足够，但无法表达"由若干子 StringID 结构化组成的 ID"——例如 `PipelineKey = compose(ObjectRenderSig, MaterialRenderSig)` 这种两级嵌套。

REQ-007 需要把 pipeline identity 从 "把一堆 hash 混在一起" 改为 "把一堆 StringID 结构化 intern 在一起"，前置要求是 `GlobalStringTable` 能支持结构化 ID 的构造与反向解构。

## 目标

1. 扩展 `GlobalStringTable` 支持 **结构化 intern**：一组 `(TypeTag, 子 StringID 列表)` 映射到一个新的 `StringID`
2. 支持 **decompose**：给定一个结构化 ID，返回其 `(TypeTag, 子 StringID 列表)`
3. 支持 **toDebugString**：递归展开到人类可读的字符串（用于日志和 `PipelineKey` 调试）
4. 为 `Intern(const char*)` / `Intern(std::string)` 提供显式命名入口，避免 `StringID` 隐式构造在模板代码里的歧义

## 需求

### R1: TypeTag 枚举

```cpp
// src/core/utils/string_table.hpp
namespace LX_core {

enum class TypeTag : uint8_t {
  String = 0,         // 叶子：普通字符串
  ShaderProgram,
  RenderState,
  VertexLayoutItem,
  VertexLayout,
  MeshRender,
  Skeleton,           // 叶子场合下也可直接用 Intern，不强制走 compose
  RenderPassEntry,
  MaterialRender,
  ObjectRender,
  PipelineKey,
};

}
```

叶子字符串 intern 时 TypeTag 默认为 `String`；结构化 compose 时由调用方显式指定。`Topology` **不**出现在枚举里——拓扑是叶子，走 `Intern("tri")`，没有子 ID 要组合。

### R2: API

```cpp
class GlobalStringTable {
public:
  static GlobalStringTable &get();

  /// 叶子字符串 intern（等价于当前的 getOrCreateID + StringID 包装）
  StringID Intern(std::string_view str);

  /// 结构化 intern：给一个 TypeTag 和一组子 StringID，
  /// 构造规范化键并映射到一个新的 StringID。
  /// 同样的 (tag, fields) 多次调用返回同一个 StringID。
  StringID compose(TypeTag tag, std::span<const StringID> fields);

  /// 反向：若 id 是由 compose 产生的，返回其 (tag, fields)。
  /// 若 id 是叶子字符串，返回 (TypeTag::String, {})。
  struct Decomposed {
    TypeTag                 tag;
    std::vector<StringID>   fields;
  };
  std::optional<Decomposed> decompose(StringID id) const;

  /// 人类可读：叶子返回原字符串；结构化递归展开为
  /// "<TagName>(<child1>, <child2>, ...)"
  std::string toDebugString(StringID id) const;

  // 已有方法保留
  const std::string &getName(uint32_t id) const;   // 叶子字符串查询

private:
  // ... 新增：
  struct ComposedEntry {
    TypeTag                 tag;
    std::vector<StringID>   fields;
  };
  std::unordered_map<uint32_t, ComposedEntry> m_composedEntries;
  // key 规范化字符串 → StringID 的映射用现有 m_stringToId 即可
};
```

### R3: 规范化键格式

`compose(tag, fields)` 的内部规范化键（用于去重）是**字符串形态**，继续复用现有 `m_stringToId`：

```
{TagName}({field1.id},{field2.id},...)
```

例如：

```
PipelineKey(42,87)
ObjectRender(13,0)
VertexLayout(3,5,7,12)   // 最后一项是 Intern(stride) 的 StringID
```

- `TagName` 是 `TypeTag` 的字符串名（由静态表提供，`TypeTag::PipelineKey → "PipelineKey"`）
- `field.id` 是子 `StringID` 的 `uint32_t` 值
- fields 为空时：`MeshRender()`
- 因为 `StringID` 本身已去重，构造确定性由 `uint32_t` 值决定

**顺序敏感**：`compose(tag, [a, b])` ≠ `compose(tag, [b, a])`——顺序要求由调用方保证（REQ-007 会显式 sort 需要稳定的部分，例如 shader variants）。

### R4: decompose 实现

`compose` 时同步在 `m_composedEntries[id] = {tag, fields}` 记下。`decompose(id)` 查表：

- 命中 → 返回 `{tag, fields}`
- 未命中但 `id` 是合法叶子 → 返回 `{TypeTag::String, {}}`
- 其他 → 返回 `std::nullopt`

### R5: toDebugString

```cpp
std::string GlobalStringTable::toDebugString(StringID id) const {
  auto d = decompose(id);
  if (!d) return "<invalid>";
  if (d->tag == TypeTag::String) return getName(id.id);

  std::string out = std::string(tagName(d->tag)) + "(";
  for (size_t i = 0; i < d->fields.size(); ++i) {
    if (i) out += ", ";
    out += toDebugString(d->fields[i]);   // 递归
  }
  out += ")";
  return out;
}
```

保护循环深度（例如 16）以防 compose 把自己作为 field 造成栈溢出——实现里加个深度参数即可，越界返回 `"<...>"`。

### R6: 线程安全

- 已有 `m_stringToId` / `m_idToString` 走 `shared_mutex`
- `m_composedEntries` 并入同一把锁（读共享、写独占）
- `compose` 路径：构造规范化键（无锁）→ 写锁查 `m_stringToId` → 未命中则写入 + 同步写入 `m_composedEntries`

### R7: 对 StringID 隐式构造的处理

当前 `StringID` 的两个构造函数 `StringID(const char*)` / `StringID(const std::string&)` **保留不动**——大量调用点依赖它们。REQ-006 只新增 `Intern(string_view)` 显式入口，两者行为等价，未来可逐步迁移。

> 规则：在 REQ-007 的结构化 compose 代码路径里**优先使用** `Intern("...")`，而不是让字符串字面量自动走 `StringID` 构造。理由是结构化构造常常嵌套，隐式构造在参数包展开时阅读性差。

## 测试

新增 `src/test/integration/test_string_table.cpp`（单元级别即可，不依赖 Vulkan）：

1. `Intern("foo") == Intern("foo")` — 叶子去重
2. `compose(PipelineKey, {a, b}) == compose(PipelineKey, {a, b})` — 结构化去重
3. `compose(PipelineKey, {a, b}) != compose(PipelineKey, {b, a})` — 顺序敏感
4. `decompose(compose(X, {a,b}))->fields == {a, b}` — 往返
5. `toDebugString(compose(PipelineKey, {Intern("foo"), compose(VertexLayout, {Intern("pos")})}))` 返回 `"PipelineKey(foo, VertexLayout(pos))"`
6. 并发 `compose` 1000 次同一 `(tag, fields)` 得到同一 ID

## 修改范围

| 文件 | 改动 |
|------|------|
| `src/core/utils/string_table.hpp` | 新增 `TypeTag` 枚举、`Intern` / `compose` / `decompose` / `toDebugString` 方法、`m_composedEntries` 字段 |
| `src/core/utils/string_table.cpp` | 新文件（若当前全在头文件 inline），承载非 inline 实现 |
| `src/test/integration/test_string_table.cpp` | 新文件，上述 6 条测试 |

## 边界与约束

- **不引入哈希碰撞检测**：规范化键用 StringID 的 uint32 值作为字段，而非嵌套字符串，所以同一组 `(tag, fields)` 永远产生同一键，不存在碰撞问题
- **内存**：`m_composedEntries` 每个 entry 多占 `TypeTag(1B) + vector<StringID>` 容量——对 pipeline 数量（通常数十到数百）可忽略
- **不支持删除**：`GlobalStringTable` 本来就是增长型的，REQ-006 延续这个约束
- **TypeTag 未来扩展**：若新增 TypeTag，需要在 `tagName()` 静态表里同步加入映射

## 依赖

无——纯 additive 扩展，可独立落地。

## 下游

- REQ-007 整体依赖本需求

## 实施状态

已完成（2026-04-13）— 通过 openspec 变更 `extend-string-table-compose` 落地。

- `src/core/utils/string_table.{hpp,cpp}` 实现 `TypeTag` / `Intern` / `compose` / `decompose` / `toDebugString`
- `src/test/integration/test_string_table.cpp` 覆盖 11 条用例（叶子去重、结构化去重、顺序敏感、跨 tag 去重、往返、叶子/非法 decompose、空 fields、toDebugString 渲染/叶子、8 线程并发 compose）
- REQ-007（`interning-pipeline-identity`）落地时已作为 `compose`/`Intern`/`TypeTag` 的首个生产消费者验证：`test_pipeline_identity.cpp` 的 `toDebugString` 输出
  `PipelineKey(ObjectRender(MeshRender(VertexLayout(...), tri), ...), MaterialRender(RenderPassEntry(ShaderProgram(blinnphong_0, HAS_NORMAL_MAP), RenderState(...))))`
  完整验证了嵌套 compose + decompose 的往返与渲染链路
