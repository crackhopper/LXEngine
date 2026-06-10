#include "core/resource/resource_uri.hpp"

namespace LX_core {
namespace {

std::string normalizePath(std::filesystem::path path) {
  return path.lexically_normal().generic_string();
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
  m_uri = normalizePath(std::move(uri));
  return *this;
}

ResourceUri ResourceUri::canonicalize(std::string_view ownerUri,
                                      std::string_view uri) {
  std::filesystem::path candidate{std::string(uri)};
  if (candidate.is_absolute()) {
    return ResourceUri(normalizePath(candidate));
  }
  std::filesystem::path owner{std::string(ownerUri)};
  const std::filesystem::path base =
      owner.has_filename() ? owner.parent_path() : owner;
  return ResourceUri(normalizePath(base / candidate));
}

} // namespace LX_core
