#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace LX_core {

class GraphResourceRegistry final {
public:
  static GraphResourceRegistry makeDefault();

  void registerResource(std::string name);
  void registerImportedResource(std::string name);
  void allowWriteMode(std::string resourceName, std::string writeMode);
  [[nodiscard]] bool contains(std::string_view name) const;
  [[nodiscard]] bool isImported(std::string_view name) const;
  [[nodiscard]] bool allowsWriteMode(std::string_view name,
                                     std::string_view writeMode) const;

private:
  std::unordered_set<std::string> m_resources;
  std::unordered_set<std::string> m_importedResources;
  std::unordered_set<std::string> m_allowedWriteModes;
};

} // namespace LX_core
