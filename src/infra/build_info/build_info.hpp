#pragma once

#include <string>

namespace LX_infra {

[[nodiscard]] std::string currentBuildInfoJson(const std::string &binaryName);
[[nodiscard]] std::string currentBuildInfoString(const std::string &binaryName);

} // namespace LX_infra
