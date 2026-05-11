#pragma once

#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <filesystem>
#include <memory>

namespace LX_demo::scene_viewer {

class SceneRuntime final {
public:
  SceneRuntime() = default;
  SceneRuntime(const SceneRuntime&) = delete;
  SceneRuntime(SceneRuntime&&) noexcept = default;
  SceneRuntime& operator=(const SceneRuntime&) = delete;
  SceneRuntime& operator=(SceneRuntime&&) noexcept = default;
  ~SceneRuntime() = default;

  void loadDefaultDocument();
  void loadFromDocumentPath(const std::filesystem::path& path);
  void saveToCurrentDocumentPath();
  void saveToDocumentPath(const std::filesystem::path& path);
  [[nodiscard]] std::filesystem::path documentPath() const;
  [[nodiscard]] LX_core::SceneSharedPtr scene() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr editorCameraNode() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr gameCameraNode() const;

private:
  std::shared_ptr<void> m_impl;
};

} // namespace LX_demo::scene_viewer
