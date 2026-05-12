#pragma once

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/scene_catalog.hpp"

#include <filesystem>
#include <memory>
#include <optional>

namespace LX_demo::lxe_editor {

class SceneRuntime final {
public:
  SceneRuntime() = default;
  SceneRuntime(const SceneRuntime&) = delete;
  SceneRuntime(SceneRuntime&&) noexcept = default;
  SceneRuntime& operator=(const SceneRuntime&) = delete;
  SceneRuntime& operator=(SceneRuntime&&) noexcept = default;
  ~SceneRuntime() = default;

  void createEmptyScene();
  void loadFromDocumentPath(
      const std::filesystem::path& path,
      std::optional<SceneSourceKind> sourceKind = std::nullopt);
  void saveToCurrentDocumentPath();
  void saveToDocumentPath(const std::filesystem::path& path);
  [[nodiscard]] std::optional<std::filesystem::path> documentPath() const;
  [[nodiscard]] std::optional<SceneSourceKind> sourceKind() const;
  [[nodiscard]] LX_core::SceneSharedPtr scene() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr editorCameraNode() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr gameCameraNode() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr
  resolveEditorHelperOwner(const std::string& path) const;

private:
  std::shared_ptr<void> m_impl;
};

} // namespace LX_demo::lxe_editor
