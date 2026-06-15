#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace LX_core {

class ResourceUri final {
public:
  ResourceUri() = default;
  ResourceUri(std::string uri);
  ResourceUri(const char *uri);
  ResourceUri(std::string_view uri);

  [[nodiscard]] static ResourceUri canonicalize(std::string_view ownerUri,
                                                std::string_view uri);

  [[nodiscard]] const std::string &string() const { return m_uri; }
  [[nodiscard]] bool empty() const { return m_uri.empty(); }

  bool operator==(const ResourceUri &other) const = default;
  bool operator==(const char *other) const { return m_uri == other; }
  bool operator==(std::string_view other) const { return m_uri == other; }
  ResourceUri &operator=(const char *uri);
  ResourceUri &operator=(std::string_view uri);
  ResourceUri &operator=(std::string uri);

private:
  std::string m_uri;
};

} // namespace LX_core
