#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace LX_demo::lxe_editor {

struct LxeEditorRuntimeState final {
  int pid = 0;
  std::string httpHost;
  std::uint16_t httpPort = 0;
  std::string wsHost;
  std::uint16_t wsPort = 0;
  std::string mcpUrl;
  std::string tokenFile;
  std::string startedAt;

  bool operator==(const LxeEditorRuntimeState&) const = default;
};

void saveLxeEditorRuntimeState(const std::filesystem::path& root,
                               const LxeEditorRuntimeState& state);
[[nodiscard]] std::optional<LxeEditorRuntimeState>
loadLxeEditorRuntimeState(const std::filesystem::path& root);

} // namespace LX_demo::lxe_editor
