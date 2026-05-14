#pragma once

#include "core/math/vec.hpp"
#include "core/math/transform.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/visibility_mask.hpp"
#include "demos/lxe_editor/editor_camera_state.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct CameraNodeState final {
  LX_core::Vec3f eye{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f target{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f up{0.0f, 1.0f, 0.0f};
  LX_core::CameraType type = LX_core::CameraType::Perspective;
  float fovY = 45.0f;
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float left = -1.0f;
  float right = 1.0f;
  float bottom = -1.0f;
  float top = 1.0f;
  LX_core::VisibilityLayerMask cullingMask = LX_core::Layer_All &
                                             ~LX_core::Layer_EditorOverlay;
};

enum class LightKind {
  Directional,
  Point,
  Spot,
};

struct LightNodeState final {
  LightKind kind = LightKind::Directional;
  LX_core::Vec3f direction{-0.3f, -1.0f, -0.5f};
  LX_core::Vec3f color{1.0f, 0.98f, 0.9f};
  float intensity = 1.0f;
  float range = 5.0f;
  float innerConeDegrees = 20.0f;
  float outerConeDegrees = 35.0f;
};

struct SceneNodeDocument final {
  std::string nodeName;
  std::string name;
  std::string parentPath;
  LX_core::Transform transform = LX_core::Transform::identity();
  LX_core::VisibilityLayerMask visibilityMask = LX_core::VisibilityMask_All;
  std::optional<std::string> meshUri;
  std::optional<std::string> materialUri;
  std::optional<CameraNodeState> camera;
  std::optional<LightNodeState> light;
  std::vector<SceneNodeDocument> children;
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
  void setGameplayCameraPath(std::string path);
  const std::string& gameplayCameraPath() const;
  SceneNodeDocument& mutableRootNode();
  const SceneNodeDocument& rootNode() const;
  bool hasEditorCamera() const;
  const EditorCameraState& editorCamera() const;
  void setEditorCamera(const EditorCameraState& state);

private:
  std::shared_ptr<void> m_impl;
};

SceneDocument loadSceneDocument(const std::filesystem::path& path);
void saveSceneDocument(const std::filesystem::path& path,
                       const SceneDocument& document);

} // namespace LX_demo::lxe_editor
