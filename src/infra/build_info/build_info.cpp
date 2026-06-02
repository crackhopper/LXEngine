#include "infra/build_info/build_info.hpp"

#include "infra/build_info/generated_build_info.hpp"

#include <string>

#ifndef LXE_BUILD_PROJECT_VERSION
#define LXE_BUILD_PROJECT_VERSION "unknown"
#endif

#ifndef LXE_BUILD_GIT_COMMIT_SHORT
#define LXE_BUILD_GIT_COMMIT_SHORT "unknown"
#endif

#ifndef LXE_BUILD_GIT_DIRTY
#define LXE_BUILD_GIT_DIRTY 0
#endif

#ifndef LXE_BUILD_TYPE
#define LXE_BUILD_TYPE "unknown"
#endif

#ifndef LXE_BUILD_PLATFORM
#define LXE_BUILD_PLATFORM "unknown"
#endif

namespace LX_infra {
namespace {

struct BuildInfo final {
  std::string projectVersion = "unknown";
  std::string gitCommitShort = "unknown";
  bool gitDirty = false;
  std::string buildType = "unknown";
  std::string platform = "unknown";
  std::string binaryName = "unknown";
};

[[nodiscard]] std::string jsonEscape(const std::string &text) {
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

[[nodiscard]] BuildInfo currentBuildInfo(const std::string &binaryName) {
  return BuildInfo{
      .projectVersion = LXE_BUILD_PROJECT_VERSION,
      .gitCommitShort = LXE_BUILD_GIT_COMMIT_SHORT,
      .gitDirty = LXE_BUILD_GIT_DIRTY != 0,
      .buildType = LXE_BUILD_TYPE,
      .platform = LXE_BUILD_PLATFORM,
      .binaryName = binaryName,
  };
}

[[nodiscard]] std::string buildVersionString(const BuildInfo &info) {
  std::string out = info.binaryName + " " + info.projectVersion;
  out += " (" + info.gitCommitShort;
  if (info.gitDirty) {
    out += "-dirty";
  }
  out += ", " + info.buildType + ", " + info.platform + ")";
  return out;
}

} // namespace

std::string currentBuildInfoJson(const std::string &binaryName) {
  const std::string buildInfo = currentBuildInfoString(binaryName);
  return "{\"buildInfo\":\"" + jsonEscape(buildInfo) + "\"}";
}

std::string currentBuildInfoString(const std::string &binaryName) {
  return buildVersionString(currentBuildInfo(binaryName));
}

} // namespace LX_infra
