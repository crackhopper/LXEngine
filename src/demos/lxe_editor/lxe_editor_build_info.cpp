#include "demos/lxe_editor/lxe_editor_build_info.hpp"

#include <string>

#ifndef LXE_EDITOR_GIT_COMMIT
#define LXE_EDITOR_GIT_COMMIT "unknown"
#endif

#ifndef LXE_EDITOR_GIT_COMMIT_SHORT
#define LXE_EDITOR_GIT_COMMIT_SHORT "unknown"
#endif

#ifndef LXE_EDITOR_GIT_DIRTY
#define LXE_EDITOR_GIT_DIRTY 0
#endif

#ifndef LXE_EDITOR_BUILD_TYPE
#define LXE_EDITOR_BUILD_TYPE "unknown"
#endif

#ifndef LXE_EDITOR_BUILT_AT
#define LXE_EDITOR_BUILT_AT "unknown"
#endif

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string jsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

} // namespace

LxeEditorBuildInfo currentLxeEditorBuildInfo() {
  return LxeEditorBuildInfo{
      .gitCommit = LXE_EDITOR_GIT_COMMIT,
      .gitCommitShort = LXE_EDITOR_GIT_COMMIT_SHORT,
      .gitDirty = LXE_EDITOR_GIT_DIRTY != 0,
      .buildType = LXE_EDITOR_BUILD_TYPE,
      .builtAt = LXE_EDITOR_BUILT_AT,
  };
}

std::string toJson(const LxeEditorBuildInfo& info) {
  std::string out = "{";
  out += "\"gitCommit\":\"";
  out += jsonEscape(info.gitCommit);
  out += "\",\"gitCommitShort\":\"";
  out += jsonEscape(info.gitCommitShort);
  out += "\",\"gitDirty\":";
  out += info.gitDirty ? "true" : "false";
  out += ",\"buildType\":\"";
  out += jsonEscape(info.buildType);
  out += "\",\"builtAt\":\"";
  out += jsonEscape(info.builtAt);
  out += "\"}";
  return out;
}

} // namespace LX_demo::lxe_editor
