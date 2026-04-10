#pragma once
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_core {

class GlobalStringTable {
public:
  static GlobalStringTable &get() {
    static GlobalStringTable instance;
    return instance;
  }

  uint32_t getOrCreateID(const std::string &name) {
    {
      std::shared_lock<std::shared_mutex> lock(m_mutex);
      auto it = m_stringToId.find(name);
      if (it != m_stringToId.end())
        return it->second;
    }

    {
      std::unique_lock<std::shared_mutex> lock(m_mutex);
      auto it = m_stringToId.find(name);
      if (it != m_stringToId.end())
        return it->second;

      uint32_t newID = m_nextID++;
      m_stringToId[name] = newID;

      if (newID >= m_idToString.size())
        m_idToString.resize(newID + 128);
      m_idToString[newID] = name;

      return newID;
    }
  }

  const std::string &getName(uint32_t id) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (id < m_idToString.size() && !m_idToString[id].empty())
      return m_idToString[id];
    static const std::string unknown = "UNKNOWN_PROPERTY";
    return unknown;
  }

private:
  GlobalStringTable() : m_nextID(1) { m_idToString.reserve(1024); }
  GlobalStringTable(const GlobalStringTable &) = delete;
  GlobalStringTable &operator=(const GlobalStringTable &) = delete;

  std::unordered_map<std::string, uint32_t> m_stringToId;
  std::vector<std::string> m_idToString;
  mutable std::shared_mutex m_mutex;
  std::atomic<uint32_t> m_nextID;
};

struct StringID {
  uint32_t id = 0;

  StringID() = default;

  StringID(const char *name)
      : id(GlobalStringTable::get().getOrCreateID(name)) {}

  StringID(const std::string &name)
      : id(GlobalStringTable::get().getOrCreateID(name)) {}

  explicit StringID(uint32_t val) : id(val) {}

  bool operator==(const StringID &rhs) const { return id == rhs.id; }
  bool operator!=(const StringID &rhs) const { return id != rhs.id; }

  struct Hash {
    size_t operator()(const StringID &p) const {
      return static_cast<size_t>(p.id);
    }
  };
};

inline StringID MakeStringID(const std::string &name) {
  return StringID(name);
}

} // namespace LX_core

namespace std {
template <> struct hash<LX_core::StringID> {
  size_t operator()(const LX_core::StringID &p) const {
    return static_cast<size_t>(p.id);
  }
};
} // namespace std
