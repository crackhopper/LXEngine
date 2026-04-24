# GlobalStringTable — 字符串驻留系统

在工业级引擎中，GlobalStringTable 通常被称为 String Interning（字符串驻留）系统。它的核心目标是：确保相同的字符串在全局范围内只有一个唯一的整数 ID。

这个系统是全自动的。你只需要使用 `StringID("name")`，系统内部会自动处理查表、插入和 ID 分配。

> 源码位置：`src/core/utils/string_table.hpp`，命名空间 `LX_core`

## 1. GlobalStringTable 实现

使用 `std::unordered_map` 配合 `std::shared_mutex`（读写锁）保证线程安全。采用单例模式，禁止拷贝。

```cpp
namespace LX_core {

class GlobalStringTable {
public:
  static GlobalStringTable &get() {
    static GlobalStringTable instance;
    return instance;
  }

  // 获取或创建一个唯一的 ID（线程安全）
  uint32_t getOrCreateID(const std::string &name) {
    // 1. 快速读取（共享锁）
    {
      std::shared_lock<std::shared_mutex> lock(m_mutex);
      auto it = m_stringToId.find(name);
      if (it != m_stringToId.end())
        return it->second;
    }
    // 2. 写入模式（排他锁 + 二次检查）
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

  // 根据 ID 反查字符串（用于调试/日志）
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
```

## 2. StringID 封装

`StringID` 是一个包装 `uint32_t` 的结构体，支持从字符串隐式构造，使用时无需手动与 `GlobalStringTable` 交互。

```cpp
struct StringID {
  uint32_t id = 0;

  StringID() = default;

  // 隐式构造：允许直接传入字符串
  // 例如：material->setFloat("u_Time", 1.0f);
  StringID(const char *name)
      : id(GlobalStringTable::get().getOrCreateID(name)) {}

  StringID(const std::string &name)
      : id(GlobalStringTable::get().getOrCreateID(name)) {}

  // 从已知 ID 构造（必须显式，防止意外转换）
  explicit StringID(uint32_t val) : id(val) {}

  bool operator==(const StringID &rhs) const { return id == rhs.id; }
  bool operator!=(const StringID &rhs) const { return id != rhs.id; }

  // 用于 std::unordered_map<StringID, T, StringID::Hash>
  struct Hash {
    size_t operator()(const StringID &p) const {
      return static_cast<size_t>(p.id);
    }
  };
};

// 便捷函数
inline StringID MakeStringID(const std::string &name) {
  return StringID(name);
}

} // namespace LX_core

// std::hash 特化，支持直接用 std::unordered_map<StringID, T>
namespace std {
template <> struct hash<LX_core::StringID> {
  size_t operator()(const LX_core::StringID &p) const {
    return static_cast<size_t>(p.id);
  }
};
}
```

## 3. 在材质系统中的应用

`MaterialTemplate` 使用 `StringID` 作为 binding cache 的键：

```cpp
class MaterialTemplate {
  // 从 Shader 反射信息构建缓存
  void buildBindingCache() {
    m_bindingCache.clear();
    for (auto &[_, entry] : m_passes) {
      auto shader = entry.shaderSet.getShader();
      for (auto &b : shader->getReflectionBindings()) {
        StringID id = MakeStringID(b.name);
        m_bindingCache[id] = b;
      }
    }
  }

  std::unordered_map<StringID, ShaderResourceBinding> m_bindingCache;
};
```

`MaterialInstance` 使用 `StringID` 作为属性存储的键：

```cpp
class MaterialInstance {
  void setFloat(StringID id, float value);   // "u_Time" 隐式转为 StringID
  void setVec4(StringID id, const Vec4f &value);
  void setTexture(StringID id, TexturePtr tex);

  std::unordered_map<StringID, Vec4f>       m_vec4s;
  std::unordered_map<StringID, float>       m_floats;
  std::unordered_map<StringID, TexturePtr>  m_textures;
};
```

调用示例：

```cpp
// "u_BaseColor" 会隐式构造为 StringID，自动查表
mat->setVec4("u_BaseColor", Vec4f{1.0f, 0.0f, 0.0f, 1.0f});
mat->setFloat("u_Time", 1.0f);
```

## 4. 生命周期

- **启动**：`GlobalStringTable` 作为单例，在第一次使用时自动初始化，ID 从 1 开始（0 保留为默认/无效值）。
- **运行中**：Shader 反射出的变量名（如 `"u_BaseColor"`）传给 `StringID` 构造函数，自动分配 ID。后续逻辑代码使用相同字符串时，返回相同 ID。自动去重，无需手动管理。
- **全局唯一**：由于单例内部有 map，无论调用多少次，同一字符串永远返回同一个 ID。

## 5. 设计优势

- **极速比较**：两个 `StringID` 的比较只是一个 `uint32_t == uint32_t`。在按材质排序等场景中性能提升显著。
- **零冗余存储**：`MaterialInstance` 里存的是数字 ID，不再是重复的字符串，内存占用大幅下降。
- **零哈希碰撞**：ID 是连续整数，作为 `unordered_map` 键时哈希分布理想。
- **热重载友好**：修改 Shader 源码后，只要变量名没变，ID 就不会变，材质实例的数据依然有效。
- **通用性**：不仅用于材质属性，还可用于渲染 Pass 名、骨骼名等任何需要频繁比较名字的地方。
