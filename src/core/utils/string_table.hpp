#pragma once
#include "core/platform/types.hpp"
#include <atomic>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LX_core {

struct StringID;

/*
@source_analysis.section TypeTag：让 StringID 不只是“压缩字符串”
`StringID` 最初只是把字符串压成整数，方便比较和做 map key。Pipeline identity
需要的不是单个名字，而是一棵可追踪的结构树：比如
`PipelineKey(MaterialTypeVariant(...), RenderPathNode(...))`。

`TypeTag` 就是这棵树每个节点的类别标签。叶子字符串走 `TypeTag::String`；
`compose(...)` 生成的结构化 ID 会记录自己的 tag 和子字段。这样我们既能保留
整数比较的速度，又能在日志里用 `toDebugString()` 还原出“这个 pipeline key
到底由哪些结构组成”。
*/
enum class TypeTag : u8 {
  String = 0,
  ShaderProgram,
  RenderState,
  VertexLayoutItem,
  VertexLayout,
  MeshRender,
  Skeleton,
  MaterialPassDefinition,
  MaterialRender,
  MaterialTypeVariant,
  ObjectRender,
  TargetRender,
  RenderPathGeometry,
  RenderPathNode,
  PipelineKey,
};

/*
@source_analysis.section GlobalStringTable：叶子 intern 与结构化 compose 共用一张表
`GlobalStringTable` 同时服务两种需求：

- 叶子名字：`StringID("Forward")`、`StringID("SceneMaterials")`
- 结构身份：`compose(TypeTag::PipelineKey, {materialTypeVariant, renderPathNode})`

这两类 ID 共用同一套整数空间和线程保护，因此上层不需要区分“普通字符串 ID”
和“结构化 ID”的存储方式。区别只存在于可选的 `m_composedEntries` 元数据里：
叶子没有子字段，结构化 ID 可以 `decompose()`，也可以递归 `toDebugString()`。

这对渲染系统很关键：hot path 里仍然只比较 `uint32_t`，而调试 pipeline identity
时又能展开结构树，不必维护一套平行的 debug 字符串。
*/
class GlobalStringTable {
public:
  static GlobalStringTable &get();

  u32 getOrCreateID(const std::string &name);
  const std::string &getName(u32 id) const;

  StringID Intern(std::string_view sv);

  StringID compose(TypeTag tag, std::span<const StringID> fields);

  struct Decomposed {
    TypeTag tag;
    std::vector<StringID> fields;
  };
  std::optional<Decomposed> decompose(StringID id) const;

  std::string toDebugString(StringID id) const;

private:
  GlobalStringTable();
  GlobalStringTable(const GlobalStringTable &) = delete;
  GlobalStringTable &operator=(const GlobalStringTable &) = delete;

  struct ComposedEntry {
    TypeTag tag;
    std::vector<StringID> fields;
  };

  std::string toDebugStringImpl(u32 id, int depth) const;

  u32 getOrCreateIDLocked(const std::string &name);

  mutable std::shared_mutex m_mutex;
  std::unordered_map<std::string, u32> m_stringToId;
  std::vector<std::string> m_idToString;
  std::unordered_map<u32, ComposedEntry> m_composedEntries;
  std::atomic<u32> m_nextID;
};

struct StringID {
  u32 id = 0;

  StringID() = default;

  StringID(const char *name)
      : id(GlobalStringTable::get().getOrCreateID(name)) {}

  StringID(const std::string &name)
      : id(GlobalStringTable::get().getOrCreateID(name)) {}

  explicit StringID(u32 val) : id(val) {}

  bool operator==(const StringID &rhs) const { return id == rhs.id; }
  bool operator!=(const StringID &rhs) const { return id != rhs.id; }

  struct Hash {
    usize operator()(const StringID &p) const {
      return static_cast<usize>(p.id);
    }
  };
};

inline StringID MakeStringID(const std::string &name) { return StringID(name); }

} // namespace LX_core

namespace std {
template <>
struct hash<LX_core::StringID> {
  usize operator()(const LX_core::StringID &p) const {
    return static_cast<usize>(p.id);
  }
};
} // namespace std
