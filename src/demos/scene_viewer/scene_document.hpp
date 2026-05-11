#pragma once

#include "core/math/vec.hpp"
#include "demos/scene_viewer/editor_camera_state.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace LX_demo::scene_viewer {

struct GameCameraState final {
  LX_core::Vec3f eye{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f target{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f up{0.0f, 1.0f, 0.0f};
  float fovY = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
};

class SceneDocument final {
public:
  SceneDocument() = default;
  SceneDocument(const SceneDocument& other);
  SceneDocument(SceneDocument&&) noexcept = default;
  SceneDocument& operator=(const SceneDocument& other);
  SceneDocument& operator=(SceneDocument&&) noexcept = default;
  ~SceneDocument() = default;

  const std::string& sceneName() const;
  void setSceneName(std::string sceneName);
  GameCameraState& mutableGameCamera();
  const GameCameraState& gameCamera() const;
  bool hasEditorCamera() const;
  const EditorCameraState& editorCamera() const;
  void setEditorCamera(const EditorCameraState& state);

private:
  std::shared_ptr<void> m_impl;
};

SceneDocument loadSceneDocument(const std::filesystem::path& path);
void saveSceneDocument(const std::filesystem::path& path,
                       const SceneDocument& document);

} // namespace LX_demo::scene_viewer
