#pragma once

#include <filesystem>
#include <string>

namespace LX_demo::lxe_editor {

class ApiTokenState final {
public:
  explicit ApiTokenState(std::filesystem::path rootDir);

  [[nodiscard]] const std::filesystem::path& rootDir() const;
  [[nodiscard]] const std::filesystem::path& tokenPath() const;
  [[nodiscard]] std::string loadOrCreateToken() const;

private:
  [[nodiscard]] static std::string generateToken();

  std::filesystem::path m_rootDir;
  std::filesystem::path m_tokenPath;
};

} // namespace LX_demo::lxe_editor
