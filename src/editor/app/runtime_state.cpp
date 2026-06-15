#include "editor/app/runtime_state.hpp"

#include "yaml-cpp/yaml.h"

#include <fstream>
#include <iostream>

namespace LX_demo::lxe_editor {
namespace {

constexpr int kRuntimeStateVersion = 1;

[[nodiscard]] std::filesystem::path
runtimeStatePath(const std::filesystem::path& root) {
  return root / "runtime_state.yaml";
}

} // namespace

void saveLxeEditorRuntimeState(const std::filesystem::path& root,
                               const LxeEditorRuntimeState& state) {
  try {
    std::filesystem::create_directories(root);
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << kRuntimeStateVersion;
    out << YAML::Key << "pid" << YAML::Value << state.pid;
    out << YAML::Key << "httpHost" << YAML::Value << state.httpHost;
    out << YAML::Key << "httpPort" << YAML::Value << state.httpPort;
    out << YAML::Key << "wsHost" << YAML::Value << state.wsHost;
    out << YAML::Key << "wsPort" << YAML::Value << state.wsPort;
    out << YAML::Key << "tokenFile" << YAML::Value << state.tokenFile;
    out << YAML::Key << "startedAt" << YAML::Value << state.startedAt;
    out << YAML::EndMap;

    std::ofstream file(runtimeStatePath(root));
    if (!file.is_open()) {
      std::cerr << "[lxe_editor] failed to open runtime state for write "
                << runtimeStatePath(root) << "\n";
      return;
    }
    file << out.c_str();
  } catch (const std::exception& e) {
    std::cerr << "[lxe_editor] failed to save runtime state "
              << runtimeStatePath(root) << ": " << e.what() << "\n";
  }
}

std::optional<LxeEditorRuntimeState>
loadLxeEditorRuntimeState(const std::filesystem::path& root) {
  const std::filesystem::path path = runtimeStatePath(root);
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }

  try {
    const YAML::Node doc = YAML::LoadFile(path.string());
    if (!doc || !doc["version"] || doc["version"].as<int>() !=
                                       kRuntimeStateVersion) {
      return std::nullopt;
    }

    LxeEditorRuntimeState state;
    state.pid = doc["pid"] ? doc["pid"].as<int>() : 0;
    state.httpHost = doc["httpHost"] ? doc["httpHost"].as<std::string>() : "";
    state.httpPort = doc["httpPort"]
                         ? static_cast<std::uint16_t>(doc["httpPort"].as<int>())
                         : 0;
    state.wsHost = doc["wsHost"] ? doc["wsHost"].as<std::string>() : "";
    state.wsPort =
        doc["wsPort"] ? static_cast<std::uint16_t>(doc["wsPort"].as<int>()) : 0;
    state.tokenFile =
        doc["tokenFile"] ? doc["tokenFile"].as<std::string>() : "";
    state.startedAt =
        doc["startedAt"] ? doc["startedAt"].as<std::string>() : "";
    return state;
  } catch (const std::exception& e) {
    std::cerr << "[lxe_editor] failed to load runtime state " << path << ": "
              << e.what() << "\n";
    return std::nullopt;
  }
}

} // namespace LX_demo::lxe_editor
