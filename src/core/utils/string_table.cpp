#include "core/utils/string_table.hpp"

#include <mutex>
#include <string>

namespace LX_core {

namespace {
constexpr int kMaxDebugDepth = 16;

std::string_view tagName(TypeTag tag) {
  switch (tag) {
  case TypeTag::String:
    return "String";
  case TypeTag::ShaderProgram:
    return "ShaderProgram";
  case TypeTag::RenderState:
    return "RenderState";
  case TypeTag::VertexLayoutItem:
    return "VertexLayoutItem";
  case TypeTag::VertexLayout:
    return "VertexLayout";
  case TypeTag::MeshRender:
    return "MeshRender";
  case TypeTag::Skeleton:
    return "Skeleton";
  case TypeTag::MaterialPassDefinition:
    return "MaterialPassDefinition";
  case TypeTag::MaterialRender:
    return "MaterialRender";
  case TypeTag::ObjectRender:
    return "ObjectRender";
  case TypeTag::PipelineKey:
    return "PipelineKey";
  }
  return "<unknown>";
}
} // namespace

GlobalStringTable &GlobalStringTable::get() {
  static GlobalStringTable instance;
  return instance;
}

GlobalStringTable::GlobalStringTable() : m_nextID(1) {
  m_idToString.reserve(1024);
}

u32 GlobalStringTable::getOrCreateID(const std::string &name) {
  {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_stringToId.find(name);
    if (it != m_stringToId.end())
      return it->second;
  }

  std::unique_lock<std::shared_mutex> lock(m_mutex);
  return getOrCreateIDLocked(name);
}

u32 GlobalStringTable::getOrCreateIDLocked(const std::string &name) {
  auto it = m_stringToId.find(name);
  if (it != m_stringToId.end())
    return it->second;

  u32 newID = m_nextID++;
  m_stringToId[name] = newID;

  if (newID >= m_idToString.size())
    m_idToString.resize(newID + 128);
  m_idToString[newID] = name;

  return newID;
}

const std::string &GlobalStringTable::getName(u32 id) const {
  std::shared_lock<std::shared_mutex> lock(m_mutex);
  if (id < m_idToString.size() && !m_idToString[id].empty())
    return m_idToString[id];
  static const std::string unknown = "UNKNOWN_PROPERTY";
  return unknown;
}

StringID GlobalStringTable::Intern(std::string_view sv) {
  return StringID(std::string(sv));
}

StringID GlobalStringTable::compose(TypeTag tag,
                                    std::span<const StringID> fields) {
  std::string key;
  key.reserve(tagName(tag).size() + 2 + fields.size() * 6);
  key.append(tagName(tag));
  key.push_back('(');
  for (usize i = 0; i < fields.size(); ++i) {
    if (i > 0)
      key.push_back(',');
    key.append(std::to_string(fields[i].id));
  }
  key.push_back(')');

  {
    std::shared_lock<std::shared_mutex> rlock(m_mutex);
    auto it = m_stringToId.find(key);
    if (it != m_stringToId.end())
      return StringID{it->second};
  }

  std::unique_lock<std::shared_mutex> wlock(m_mutex);
  auto existing = m_stringToId.find(key);
  if (existing != m_stringToId.end())
    return StringID{existing->second};

  u32 newID = getOrCreateIDLocked(key);
  ComposedEntry entry;
  entry.tag = tag;
  entry.fields.assign(fields.begin(), fields.end());
  m_composedEntries.emplace(newID, std::move(entry));
  return StringID{newID};
}

std::optional<GlobalStringTable::Decomposed>
GlobalStringTable::decompose(StringID id) const {
  std::shared_lock<std::shared_mutex> lock(m_mutex);
  auto it = m_composedEntries.find(id.id);
  if (it != m_composedEntries.end()) {
    Decomposed result;
    result.tag = it->second.tag;
    result.fields = it->second.fields;
    return result;
  }

  if (id.id != 0 && id.id < m_idToString.size() &&
      !m_idToString[id.id].empty()) {
    return Decomposed{TypeTag::String, {}};
  }

  return std::nullopt;
}

std::string GlobalStringTable::toDebugString(StringID id) const {
  return toDebugStringImpl(id.id, 0);
}

std::string GlobalStringTable::toDebugStringImpl(u32 id, int depth) const {
  if (depth > kMaxDebugDepth)
    return "<...>";

  std::shared_lock<std::shared_mutex> lock(m_mutex);

  auto composedIt = m_composedEntries.find(id);
  if (composedIt != m_composedEntries.end()) {
    TypeTag tag = composedIt->second.tag;
    std::vector<StringID> fields = composedIt->second.fields;
    lock.unlock();

    std::string out;
    out.append(tagName(tag));
    out.push_back('(');
    for (usize i = 0; i < fields.size(); ++i) {
      if (i > 0)
        out.append(", ");
      out.append(toDebugStringImpl(fields[i].id, depth + 1));
    }
    out.push_back(')');
    return out;
  }

  if (id != 0 && id < m_idToString.size() && !m_idToString[id].empty())
    return m_idToString[id];

  return "<invalid>";
}

} // namespace LX_core
