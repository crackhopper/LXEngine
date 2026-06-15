#include "core/resource/resource_uri.hpp"

namespace LX_core {
namespace {

[[nodiscard]] std::string normalizeFilesystemPath(std::filesystem::path path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] std::string normalizePath(std::string_view value) {
  const std::string input(value);
  const std::string schemeSeparator = "://";
  const std::size_t schemePos = input.find(schemeSeparator);
  if (schemePos == std::string::npos) {
    return normalizeFilesystemPath(input);
  }

  const std::string prefix =
      input.substr(0, schemePos + schemeSeparator.size());
  const std::string path = input.substr(schemePos + schemeSeparator.size());
  return prefix + normalizeFilesystemPath(path);
}

} // namespace

ResourceUri::ResourceUri(std::string uri) : m_uri(normalizePath(uri)) {}
ResourceUri::ResourceUri(const char *uri) : ResourceUri(std::string(uri)) {}
ResourceUri::ResourceUri(std::string_view uri)
    : ResourceUri(std::string(uri)) {}

ResourceUri &ResourceUri::operator=(const char *uri) {
  m_uri = normalizePath(std::string(uri));
  return *this;
}

ResourceUri &ResourceUri::operator=(std::string_view uri) {
  m_uri = normalizePath(std::string(uri));
  return *this;
}

ResourceUri &ResourceUri::operator=(std::string uri) {
  m_uri = normalizePath(uri);
  return *this;
}

ResourceUri ResourceUri::canonicalize(std::string_view ownerUri,
                                      std::string_view uri) {
  const std::string uriText(uri);
  if (uriText.find("://") != std::string::npos) {
    return ResourceUri(uriText);
  }

  const std::string ownerText(ownerUri);
  const std::string schemeSeparator = "://";
  const std::size_t schemePos = ownerText.find(schemeSeparator);
  if (schemePos != std::string::npos) {
    const std::string prefix =
        ownerText.substr(0, schemePos + schemeSeparator.size());
    const std::filesystem::path ownerPath{
        ownerText.substr(schemePos + schemeSeparator.size())};
    const std::filesystem::path base =
        ownerPath.has_filename() ? ownerPath.parent_path() : ownerPath;
    return ResourceUri(prefix + normalizeFilesystemPath(base / uriText));
  }

  std::filesystem::path candidate{std::string(uri)};
  if (candidate.is_absolute()) {
    return ResourceUri(normalizeFilesystemPath(candidate));
  }
  std::filesystem::path owner{std::string(ownerUri)};
  const std::filesystem::path base =
      owner.has_filename() ? owner.parent_path() : owner;
  return ResourceUri(normalizeFilesystemPath(base / candidate));
}

} // namespace LX_core
