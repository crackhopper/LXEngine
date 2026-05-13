#pragma once

#include <string>

namespace LX_demo::lxe_editor {

struct LxeEditorBuildInfo final {
  std::string gitCommit;
  std::string gitCommitShort;
  bool gitDirty = false;
  std::string buildType;
  std::string builtAt;
};

[[nodiscard]] LxeEditorBuildInfo currentLxeEditorBuildInfo();
[[nodiscard]] std::string toJson(const LxeEditorBuildInfo& info);

} // namespace LX_demo::lxe_editor
