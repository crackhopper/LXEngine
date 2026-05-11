#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace LX_demo::scene_viewer {

struct EditorDataDocument final {
  int version = 1;
  std::vector<std::string> consoleHistory;
};

class EditorDataState final {
public:
  explicit EditorDataState(std::filesystem::path rootDir);

  [[nodiscard]] const std::filesystem::path& dataPath() const;
  [[nodiscard]] EditorDataDocument load() const;
  bool save(const EditorDataDocument& document) const;

private:
  std::filesystem::path m_rootDir;
  std::filesystem::path m_dataPath;
};

} // namespace LX_demo::scene_viewer
