#pragma once

#include "core/asset/material_instance.hpp"
#include "core/math/transform.hpp"
#include "core/math/vec.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/scene/camera.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/visibility_mask.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace LX_infra::scene_io {

struct EditorCameraState final {
  LX_core::Vec3f position{0.0f, 0.0f, 0.0f};
  LX_core::Vec3f rotationEulerDeg{0.0f, 0.0f, 0.0f};
  float fovY = 45.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;

  [[nodiscard]] static EditorCameraState
  captureFrom(const LX_core::SceneNode &node,
              const LX_core::CameraComponent &camera);

  void applyTo(LX_core::SceneNode &node,
               LX_core::CameraComponent &camera) const;
  void applyToNode(LX_core::SceneNode &node) const;
  void applyToCamera(LX_core::CameraComponent &camera) const;
};

struct CameraNodeState final {
  LX_core::CameraType type = LX_core::CameraType::Perspective;
  float fovY = 45.0f;
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float orthographicHeight = 2.0f;
  float focusDistance = 1.0f;
  LX_core::VisibilityLayerMask cullingMask =
      LX_core::Layer_All & ~LX_core::Layer_EditorOverlay;
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
  float shadowStrength = 0.45f;
  float shadowDistance = 80.0f;
  u32 shadowCascadeCount = LX_core::MaxShadowCascades;
  float range = 5.0f;
  float innerConeDegrees = 20.0f;
  float outerConeDegrees = 35.0f;
  std::optional<std::string> offlineYaml;
};

struct MaterialOverrideState final {
  std::optional<LX_core::Vec3f> baseColor;
  std::unordered_map<std::string, LX_core::MaterialParameterValue> parameters;

  [[nodiscard]] bool empty() const {
    return !baseColor.has_value() && parameters.empty();
  }
};

struct ProceduralMaterialState final {
  bool enabled = false;
  std::string binding = "ShadertoyUBO";
  std::string timeMember = "time";
  std::string resolutionMember = "resolution";
  std::optional<std::string> audioBandsMember = "audioBands";
  std::optional<std::string> audioChannelBinding = "iChannel0";

  [[nodiscard]] bool empty() const { return !enabled; }
};

struct EnvironmentState final {
  bool enabled = false;
  std::string hdrUri;
  bool skyboxEnabled = true;
  float intensity = 1.0f;
  float roughnessMipCount = 5.0f;

  [[nodiscard]] bool empty() const { return !enabled && hdrUri.empty(); }
};

struct MaterialBindingDocument final {
  std::string tag;
  std::string uri;
  std::string source;
  std::optional<std::string> offlineYaml;
  MaterialOverrideState materialOverrides;
  MaterialOverrideState nodeMaterialOverrides;
};

struct SceneNodeDocument final {
  std::string nodeName;
  std::string name;
  std::string parentPath;
  LX_core::Transform transform = LX_core::Transform::identity();
  LX_core::VisibilityLayerMask visibilityMask = LX_core::VisibilityMask_All;
  std::optional<std::string> meshUri;
  std::optional<std::string> meshOfflineYaml;
  std::optional<std::string> materialUri;
  std::optional<std::string> materialOfflineYaml;
  std::vector<MaterialBindingDocument> materials;
  ProceduralMaterialState proceduralMaterial;
  MaterialOverrideState nodeMaterialOverrides;
  MaterialOverrideState materialOverrides;
  std::optional<CameraNodeState> camera;
  std::optional<std::string> cameraOfflineYaml;
  std::optional<LightNodeState> light;
  std::optional<std::string> offlineYaml;
  std::vector<SceneNodeDocument> children;
};

class SceneDocument final {
public:
  SceneDocument() = default;
  SceneDocument(const SceneDocument &other);
  SceneDocument(SceneDocument &&) noexcept = default;
  SceneDocument &operator=(const SceneDocument &other);
  SceneDocument &operator=(SceneDocument &&) noexcept = default;
  ~SceneDocument() = default;

  const std::string &sceneName() const;
  void setSceneName(std::string sceneName);
  void setGameplayCameraPath(std::string path);
  const std::string &gameplayCameraPath() const;
  bool hasEnvironment() const;
  const EnvironmentState &environment() const;
  void setEnvironment(EnvironmentState state);
  bool hasRenderProfileDocument() const;
  const LX_core::offline::RenderProfileDocument &renderProfileDocument() const;
  void setRenderProfileDocument(
      LX_core::offline::RenderProfileDocument profiles);
  SceneNodeDocument &mutableRootNode();
  const SceneNodeDocument &rootNode() const;
  bool hasEditorCamera() const;
  const EditorCameraState &editorCamera() const;
  void setEditorCamera(const EditorCameraState &state);

private:
  std::shared_ptr<void> m_impl;
};

SceneDocument loadSceneDocument(const std::filesystem::path &path);
void saveSceneDocument(const std::filesystem::path &path,
                       const SceneDocument &document);

[[nodiscard]] bool isValidCacheUri(const std::string &uri);
void validateSceneAssetUri(const std::string &uri, const char *fieldName);

} // namespace LX_infra::scene_io
