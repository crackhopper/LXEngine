#pragma once

#include "demos/scene_viewer/scene_catalog.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace LX_demo::scene_viewer {

enum class ScenePermissionLevel {
  User,
  Admin,
};

struct SaveDecision final {
  std::filesystem::path path;
  SceneSourceKind kind = SceneSourceKind::Local;
  bool redirectedFromAsset = false;
};

class SceneSession final {
public:
  using TimestampProvider = std::function<std::string()>;

  SceneSession(std::filesystem::path localRoot, TimestampProvider timestampProvider);

  ScenePermissionLevel permission() const;
  void setPermission(ScenePermissionLevel permission);

  bool isDirty() const;
  void setDirty(bool dirty);

  void setCurrentDocument(std::optional<std::filesystem::path> path,
                          std::optional<SceneSourceKind> kind);
  const std::optional<std::filesystem::path>& currentDocumentPath() const;
  const std::optional<SceneSourceKind>& currentSourceKind() const;

  SaveDecision decideSaveTarget(const std::optional<std::filesystem::path>& explicitPath,
                                const std::string& sceneName) const;

private:
  std::filesystem::path buildTimestampedLocalPath(const std::string& sceneName,
                                                  const std::filesystem::path& preferredStemSource) const;
  static std::string sanitizeStem(std::string stem);

  std::filesystem::path m_localRoot;
  TimestampProvider m_timestampProvider;
  ScenePermissionLevel m_permission = ScenePermissionLevel::User;
  bool m_dirty = false;
  std::optional<std::filesystem::path> m_currentDocumentPath;
  std::optional<SceneSourceKind> m_currentSourceKind;
};

} // namespace LX_demo::scene_viewer
