#include "demos/lxe_editor/editor_data_state.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace LX_demo::lxe_editor {
namespace {

constexpr int kEditorDataVersion = 1;
constexpr size_t kMaxConsoleHistoryEntries = 50;

void clampConsoleHistory(std::vector<std::string>& history) {
  history.erase(std::remove_if(history.begin(), history.end(),
                               [](const std::string& line) {
                                 return line.empty();
                               }),
                history.end());
  if (history.size() > kMaxConsoleHistoryEntries) {
    history.erase(history.begin(),
                  history.begin() +
                      static_cast<std::ptrdiff_t>(history.size() -
                                                  kMaxConsoleHistoryEntries));
  }
}

} // namespace

EditorDataState::EditorDataState(std::filesystem::path rootDir)
    : m_rootDir(std::move(rootDir)),
      m_dataPath(m_rootDir / "editor_data.yaml") {}

const std::filesystem::path& EditorDataState::dataPath() const {
  return m_dataPath;
}

EditorDataDocument EditorDataState::load() const {
  EditorDataDocument document;
  if (!std::filesystem::exists(m_dataPath)) {
    return document;
  }

  try {
    const YAML::Node root = YAML::LoadFile(m_dataPath.string());
    if (const auto versionNode = root["version"]; versionNode) {
      document.version = versionNode.as<int>();
    }
    if (document.version != kEditorDataVersion) {
      std::cerr << "[lxe_editor] unsupported editor data version in "
                << m_dataPath << ", using defaults\n";
      return EditorDataDocument{};
    }

    if (const auto lastProjectNode = root["lastProject"];
        lastProjectNode && lastProjectNode.IsScalar()) {
      document.lastProject = lastProjectNode.as<std::string>();
    }

    if (const auto historyNode = root["consoleHistory"];
        historyNode && historyNode.IsSequence()) {
      for (const auto& entryNode : historyNode) {
        if (!entryNode || !entryNode.IsScalar()) {
          continue;
        }
        document.consoleHistory.push_back(entryNode.as<std::string>());
      }
    }
    clampConsoleHistory(document.consoleHistory);
  } catch (const std::exception& e) {
    std::cerr << "[lxe_editor] failed to load editor data " << m_dataPath
              << ": " << e.what() << "\n";
    return EditorDataDocument{};
  }

  return document;
}

bool EditorDataState::save(const EditorDataDocument& sourceDocument) const {
  EditorDataDocument document = sourceDocument;
  document.version = kEditorDataVersion;
  clampConsoleHistory(document.consoleHistory);

  std::error_code ec;
  std::filesystem::create_directories(m_rootDir, ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to create editor data directory "
              << m_rootDir << ": " << ec.message() << "\n";
    return false;
  }

  YAML::Emitter out;
  out << YAML::BeginMap;
  out << YAML::Key << "version" << YAML::Value << document.version;
  if (document.lastProject.has_value()) {
    out << YAML::Key << "lastProject" << YAML::Value
        << document.lastProject->generic_string();
  }
  out << YAML::Key << "consoleHistory" << YAML::Value << YAML::BeginSeq;
  for (const auto& line : document.consoleHistory) {
    out << line;
  }
  out << YAML::EndSeq;
  out << YAML::EndMap;

  std::ofstream file(m_dataPath,
                     std::ios::out | std::ios::binary | std::ios::trunc);
  if (!file) {
    std::cerr << "[lxe_editor] failed to open editor data for write "
              << m_dataPath << "\n";
    return false;
  }
  file << out.c_str();
  return static_cast<bool>(file);
}

} // namespace LX_demo::lxe_editor
