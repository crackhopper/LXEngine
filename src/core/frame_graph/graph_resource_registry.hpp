#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace LX_core {

class GraphResourceRegistry final {
public:
  static GraphResourceRegistry makeDefault();

  void registerResource(std::string name);
  [[nodiscard]] bool contains(std::string_view name) const;

private:
  std::unordered_set<std::string> m_resources;
};

} // namespace LX_core
