#pragma once

#include "core/asset/material_instance.hpp"
#include "core/editor/command_bus.hpp"
#include "core/math/vec.hpp"
#include "core/scene/light.hpp"
#include "core/scene/scene_events.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core {

class EditorState;
class Scene;
class SceneNode;

struct MaterialParameterEditorValue final {
  std::string binding;
  std::string member;
  MaterialParameterValue value;
  bool runtimeOwned = false;
};

struct InspectorMaterialCallbacks {
  std::function<std::optional<std::string>(const std::string &path)>
      materialUri;
  std::function<std::optional<Vec3f>(const std::string &path)> nodeBaseColor;
  std::function<bool(const std::string &path)> canEditBaseColor;
  std::function<std::optional<bool>(const std::string &path)>
      proceduralMaterialEnabled;
  std::function<std::vector<std::string>()> presets;
  std::function<std::vector<MaterialParameterEditorValue>(
      const std::string &path)>
      materialParameters;
};

class InspectorPanel final {
public:
  struct Snapshot {
    bool hasSelection = false;
    std::string path;
    std::string name;
    Vec3f translation{0.0f, 0.0f, 0.0f};
    Vec3f rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    Vec3f scale{1.0f, 1.0f, 1.0f};
    u32 visibilityMask = 0;
    bool visible = false;
    bool hasCamera = false;
    float cameraFov = 45.0f;
    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;
    bool cameraPerspective = true;
    u32 cameraCullingMask = 0;
    bool hasLight = false;
    std::string lightKind;
    Vec3f lightDirection{0.0f, -1.0f, 0.0f};
    Vec3f lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 1.0f;
    float lightShadowStrength = 0.45f;
    float lightShadowBias = 0.02f;
    float lightShadowDistance = 80.0f;
    u32 lightShadowCascadeCount = MaxShadowCascades;
    float lightRange = 5.0f;
    float lightInnerConeDegrees = 20.0f;
    float lightOuterConeDegrees = 35.0f;
    bool hasMesh = false;
    bool hasMaterial = false;
    bool hasSkeleton = false;
    bool hasMaterialSection = false;
    std::string materialUri;
    bool hasNodeBaseColorOverride = false;
    Vec3f nodeBaseColorOverride{0.8f, 0.8f, 0.8f};
    bool canEditBaseColor = false;
    bool hasProceduralMaterialEnabled = false;
    bool proceduralMaterialEnabled = false;
    std::vector<std::string> materialPresets;
    std::vector<MaterialParameterEditorValue> materialParameters;
  };

  InspectorPanel(CommandBus &commandBus, EditorState &editorState,
                 InspectorMaterialCallbacks materialCallbacks = {});

  void draw();

  [[nodiscard]] Snapshot makeSnapshot() const;
  [[nodiscard]] CommandResult dispatchRename(std::string_view path,
                                             std::string_view newName);
  [[nodiscard]] CommandResult dispatchSetVec3(std::string_view path,
                                              std::string_view field,
                                              const Vec3f &value);
  [[nodiscard]] CommandResult
  dispatchSetFloat(std::string_view path, std::string_view field, float value);
  [[nodiscard]] CommandResult
  dispatchSetUnsigned(std::string_view path, std::string_view field, u32 value);
  [[nodiscard]] CommandResult dispatchSetVisible(std::string_view path,
                                                 bool visible);
  [[nodiscard]] CommandResult dispatchSetToken(std::string_view path,
                                               std::string_view field,
                                               std::string_view value);
  [[nodiscard]] CommandResult
  dispatchApplyMaterialOverride(std::string_view path, std::string_view field);
  [[nodiscard]] static std::vector<std::string>
  discoverExperimentMaterialCandidates(
      const std::filesystem::path &materialsDir);
  [[nodiscard]] CommandResult dispatchMove(std::string_view path,
                                           const Vec3f &translation);
  [[nodiscard]] CommandResult dispatchRotate(std::string_view path,
                                             const Vec3f &rotationEulerDegrees);
  [[nodiscard]] CommandResult dispatchScale(std::string_view path,
                                            const Vec3f &scale);
  [[nodiscard]] bool isOpen() const;
  void setOpen(bool open);

private:
  void refreshSceneSubscription();
  void handleSceneEvent(const SceneEvent &event);
  [[nodiscard]] bool shouldInvalidateForEvent(const SceneEvent &event) const;
  void syncDraftFromSnapshot(const Snapshot &snapshot);
  void drawSelection(const Snapshot &snapshot);

  CommandBus &m_commandBus;
  EditorState &m_editorState;
  InspectorMaterialCallbacks m_materialCallbacks;
  SceneEventSubscription m_sceneSubscription;
  std::weak_ptr<Scene> m_subscribedScene;
  std::string m_syncedSelectionPath;
  bool m_snapshotDirty = true;
  std::array<char, 256> m_nameBuffer{};
  Vec3f m_translationDraft{0.0f, 0.0f, 0.0f};
  Vec3f m_rotationDraft{0.0f, 0.0f, 0.0f};
  Vec3f m_scaleDraft{1.0f, 1.0f, 1.0f};
  u32 m_visibilityMaskDraft = 0;
  float m_cameraFovDraft = 45.0f;
  float m_cameraNearDraft = 0.1f;
  float m_cameraFarDraft = 1000.0f;
  int m_cameraProjectionDraft = 0;
  u32 m_cameraCullingMaskDraft = 0;
  Vec3f m_lightDirectionDraft{0.0f, -1.0f, 0.0f};
  Vec3f m_lightColorDraft{1.0f, 1.0f, 1.0f};
  float m_lightIntensityDraft = 1.0f;
  float m_lightShadowStrengthDraft = 0.45f;
  float m_lightShadowBiasDraft = 0.02f;
  float m_lightShadowDistanceDraft = 80.0f;
  u32 m_lightShadowCascadeCountDraft = MaxShadowCascades;
  std::array<char, 256> m_visibilityMaskBuffer{};
  std::array<char, 256> m_cameraCullingMaskBuffer{};
  std::array<char, 512> m_materialUriBuffer{};
  int m_materialPresetDraft = -1;
  Vec3f m_nodeBaseColorDraft{0.8f, 0.8f, 0.8f};
  float m_lightRangeDraft = 5.0f;
  float m_lightInnerConeDraft = 20.0f;
  float m_lightOuterConeDraft = 35.0f;
  bool m_open = true;
};

} // namespace LX_core
