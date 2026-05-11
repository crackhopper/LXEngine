#include "demos/scene_viewer/scene_session.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace LX_demo::scene_viewer {

SceneSession::SceneSession(std::filesystem::path localRoot,
                           TimestampProvider timestampProvider)
    : m_localRoot(std::move(localRoot)),
      m_timestampProvider(std::move(timestampProvider)) {}

ScenePermissionLevel SceneSession::permission() const { return m_permission; }

void SceneSession::setPermission(const ScenePermissionLevel permission) {
  m_permission = permission;
}

bool SceneSession::isDirty() const { return m_dirty; }

void SceneSession::setDirty(const bool dirty) { m_dirty = dirty; }

void SceneSession::setCurrentDocument(
    std::optional<std::filesystem::path> path,
    std::optional<SceneSourceKind> kind) {
  if (path.has_value()) {
    path = std::filesystem::absolute(*path).lexically_normal();
  }
  m_currentDocumentPath = std::move(path);
  m_currentSourceKind = std::move(kind);
}

const std::optional<std::filesystem::path>& SceneSession::currentDocumentPath() const {
  return m_currentDocumentPath;
}

const std::optional<SceneSourceKind>& SceneSession::currentSourceKind() const {
  return m_currentSourceKind;
}

SaveDecision SceneSession::decideSaveTarget(
    const std::optional<std::filesystem::path>& explicitPath,
    const std::string& sceneName) const {
  if (explicitPath.has_value()) {
    const std::filesystem::path normalized =
        std::filesystem::absolute(*explicitPath).lexically_normal();
    const bool localExplicit =
        normalized.string().find(m_localRoot.lexically_normal().string()) == 0;
    if (localExplicit || m_permission == ScenePermissionLevel::Admin) {
      return SaveDecision{.path = normalized,
                          .kind = localExplicit ? SceneSourceKind::Local
                                                : SceneSourceKind::Asset,
                          .redirectedFromAsset = false};
    }

    return SaveDecision{
        .path = buildTimestampedLocalPath(sceneName, normalized),
        .kind = SceneSourceKind::Local,
        .redirectedFromAsset = true,
    };
  }

  if (m_currentDocumentPath.has_value() && m_currentSourceKind.has_value()) {
    if (*m_currentSourceKind == SceneSourceKind::Local) {
      return SaveDecision{.path = *m_currentDocumentPath,
                          .kind = SceneSourceKind::Local,
                          .redirectedFromAsset = false};
    }
    if (m_permission == ScenePermissionLevel::Admin) {
      return SaveDecision{.path = *m_currentDocumentPath,
                          .kind = SceneSourceKind::Asset,
                          .redirectedFromAsset = false};
    }
    return SaveDecision{
        .path = buildTimestampedLocalPath(sceneName, *m_currentDocumentPath),
        .kind = SceneSourceKind::Local,
        .redirectedFromAsset = true,
    };
  }

  return SaveDecision{
      .path = buildTimestampedLocalPath(sceneName, std::filesystem::path{}),
      .kind = SceneSourceKind::Local,
      .redirectedFromAsset = false,
  };
}

std::filesystem::path SceneSession::buildTimestampedLocalPath(
    const std::string& sceneName,
    const std::filesystem::path& preferredStemSource) const {
  std::string stem;
  if (!preferredStemSource.empty()) {
    stem = preferredStemSource.stem().string();
    if (preferredStemSource.extension() == ".yaml") {
      stem = preferredStemSource.stem().stem().string();
    }
  }
  if (stem.empty()) {
    stem = sceneName;
  }
  stem = sanitizeStem(std::move(stem));
  if (stem.empty()) {
    stem = "scene";
  }

  return m_localRoot / (stem + "." + m_timestampProvider() + ".scene.yaml");
}

std::string SceneSession::sanitizeStem(std::string stem) {
  std::replace_if(
      stem.begin(), stem.end(),
      [](const unsigned char ch) {
        return !(std::isalnum(ch) || ch == '_' || ch == '-');
      },
      '_');
  return stem;
}

} // namespace LX_demo::scene_viewer
