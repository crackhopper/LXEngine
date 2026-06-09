#include "core/editor/inspector_panel.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <utility>

namespace LX_core {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;

[[nodiscard]] std::string trim(std::string_view text) {
  usize begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' ||
                                 text[begin] == '\r' || text[begin] == '\n')) {
    ++begin;
  }

  usize end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] std::string quoteToken(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] std::string formatFloat(const float value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3) << value;
  return oss.str();
}

[[nodiscard]] std::string formatUnsigned(const u32 value) {
  return std::to_string(value);
}

[[nodiscard]] std::string
formatMaterialParameterCommandArgs(const MaterialParameterValue &value) {
  switch (value.type) {
  case MaterialParameterValueType::Float:
    return formatFloat(value.floatValue);
  case MaterialParameterValueType::Int:
    return std::to_string(value.intValue);
  case MaterialParameterValueType::Vec3:
    return formatFloat(value.vectorValue.x) + " " +
           formatFloat(value.vectorValue.y) + " " +
           formatFloat(value.vectorValue.z);
  case MaterialParameterValueType::Vec4:
    return formatFloat(value.vectorValue.x) + " " +
           formatFloat(value.vectorValue.y) + " " +
           formatFloat(value.vectorValue.z) + " " +
           formatFloat(value.vectorValue.w);
  }
  return {};
}

[[nodiscard]] std::string formatMask(const u32 value) {
  std::ostringstream oss;
  oss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
      << value;
  return oss.str();
}

[[nodiscard]] std::optional<u32> parseUnsignedText(std::string_view text) {
  const std::string trimmed = trim(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  try {
    size_t parsed = 0;
    const unsigned long value = std::stoul(trimmed, &parsed, 0);
    if (parsed != trimmed.size() || value > std::numeric_limits<u32>::max()) {
      return std::nullopt;
    }
    return static_cast<u32>(value);
  } catch (...) {
    return std::nullopt;
  }
}

void copyToBuffer(std::string_view text, std::span<char> buffer) {
  std::fill(buffer.begin(), buffer.end(), '\0');
  const usize copyLength = std::min(text.size(), buffer.size() - 1);
  if (copyLength > 0) {
    std::memcpy(buffer.data(), text.data(), copyLength);
  }
}

[[nodiscard]] std::optional<std::reference_wrapper<const LightBase>>
findLightForNode(const SceneNode &node) {
  const auto scene = node.getAttachedScene();
  if (!scene) {
    return std::nullopt;
  }
  return scene->getLight(node);
}

[[nodiscard]] Vec3f quatToEulerDegrees(const Quatf &quat) {
  const Quatf q = quat.normalized();

  const float sinrCosp = 2.0f * (q.w * q.v.x + q.v.y * q.v.z);
  const float cosrCosp = 1.0f - 2.0f * (q.v.x * q.v.x + q.v.y * q.v.y);
  const float roll = std::atan2(sinrCosp, cosrCosp);

  const float sinp = 2.0f * (q.w * q.v.y - q.v.z * q.v.x);
  const float pitch = std::abs(sinp) >= 1.0f ? std::copysign(0.5f * kPi, sinp)
                                             : std::asin(sinp);

  const float sinyCosp = 2.0f * (q.w * q.v.z + q.v.x * q.v.y);
  const float cosyCosp = 1.0f - 2.0f * (q.v.y * q.v.y + q.v.z * q.v.z);
  const float yaw = std::atan2(sinyCosp, cosyCosp);

  return Vec3f{roll * kRadToDeg, pitch * kRadToDeg, yaw * kRadToDeg};
}

} // namespace

InspectorPanel::InspectorPanel(CommandBus &commandBus, EditorState &editorState,
                               InspectorMaterialCallbacks materialCallbacks)
    : m_commandBus(commandBus), m_editorState(editorState),
      m_materialCallbacks(std::move(materialCallbacks)) {}

bool InspectorPanel::isOpen() const { return m_open; }

void InspectorPanel::setOpen(const bool open) { m_open = open; }

void InspectorPanel::draw() {
  refreshSceneSubscription();

  if (!m_open) {
    return;
  }

  if (!ImGui::Begin("Inspector", &m_open)) {
    ImGui::End();
    return;
  }

  const Snapshot snapshot = makeSnapshot();
  if (!snapshot.hasSelection) {
    ImGui::TextUnformatted("No selection");
    m_syncedSelectionPath.clear();
    m_snapshotDirty = true;
    ImGui::End();
    return;
  }

  if (m_snapshotDirty || snapshot.path != m_syncedSelectionPath) {
    syncDraftFromSnapshot(snapshot);
  }

  drawSelection(snapshot);
  ImGui::End();
}

InspectorPanel::Snapshot InspectorPanel::makeSnapshot() const {
  Snapshot snapshot;
  const auto selected = m_editorState.getPrimarySelected();
  if (!selected.has_value()) {
    return snapshot;
  }

  SceneNode &node = selected->get();
  snapshot.hasSelection = true;
  snapshot.path = node.getPath();
  snapshot.name = node.getName();
  snapshot.translation = node.getTranslation();
  snapshot.rotationEulerDegrees = quatToEulerDegrees(node.getRotation());
  snapshot.scale = node.getScale();
  snapshot.visibilityMask = node.getVisibilityLayerMask();
  snapshot.hasCamera = node.getComponent<CameraComponent>().has_value();
  if (snapshot.hasCamera) {
    const auto camera = node.getComponent<CameraComponent>();
    snapshot.cameraFov = camera->get().getFovY();
    snapshot.cameraNear = camera->get().getNearPlane();
    snapshot.cameraFar = camera->get().getFarPlane();
    snapshot.cameraPerspective =
        camera->get().getProjectionType() == CameraType::Perspective;
    snapshot.cameraCullingMask = camera->get().getCullingMask();
  }
  if (const auto light = findLightForNode(node)) {
    snapshot.hasLight = true;
    const LightBase &lightRef = light->get();
    if (const auto *directional =
            dynamic_cast<const DirectionalLight *>(&lightRef)) {
      snapshot.lightKind = "Directional";
      snapshot.lightDirection = directional->getDirection();
      snapshot.lightColor = directional->getColor();
      snapshot.lightIntensity = directional->getIntensity();
      snapshot.lightShadowStrength = directional->getShadowParams().z;
      snapshot.lightShadowBias = directional->getShadowParams().y;
      snapshot.lightShadowDistance = directional->getShadowDistance();
      snapshot.lightShadowCascadeCount = directional->getShadowCascadeCount();
    } else if (const auto *point = dynamic_cast<const PointLight *>(&lightRef)) {
      snapshot.lightKind = "Point";
      snapshot.lightColor = point->getColor();
      snapshot.lightIntensity = point->getIntensity();
      snapshot.lightRange = point->getRange();
    } else if (const auto *spot = dynamic_cast<const SpotLight *>(&lightRef)) {
      snapshot.lightKind = "Spot";
      snapshot.lightDirection = spot->getDirection();
      snapshot.lightColor = spot->getColor();
      snapshot.lightIntensity = spot->getIntensity();
      snapshot.lightRange = spot->getRange();
      snapshot.lightInnerConeDegrees = spot->getInnerConeDegrees();
      snapshot.lightOuterConeDegrees = spot->getOuterConeDegrees();
    }
  }
  snapshot.hasMesh = node.getComponent<MeshComponent>().has_value();
  snapshot.hasMaterial = node.getComponent<MaterialComponent>().has_value();
  snapshot.hasSkeleton = node.getComponent<SkeletonComponent>().has_value();
  if (m_materialCallbacks.materialUri) {
    if (const auto uri = m_materialCallbacks.materialUri(snapshot.path);
        uri.has_value()) {
      snapshot.materialUri = *uri;
    }
  }
  snapshot.hasMaterialSection = (snapshot.hasMesh && snapshot.hasMaterial) ||
                                !snapshot.materialUri.empty();
  if (m_materialCallbacks.nodeBaseColor) {
    if (const auto color = m_materialCallbacks.nodeBaseColor(snapshot.path);
        color.has_value()) {
      snapshot.hasNodeBaseColorOverride = true;
      snapshot.nodeBaseColorOverride = *color;
    }
  }
  if (m_materialCallbacks.canEditBaseColor) {
    snapshot.canEditBaseColor =
        m_materialCallbacks.canEditBaseColor(snapshot.path);
  }
  if (m_materialCallbacks.proceduralMaterialEnabled) {
    if (const auto enabled =
            m_materialCallbacks.proceduralMaterialEnabled(snapshot.path);
        enabled.has_value()) {
      snapshot.hasProceduralMaterialEnabled = true;
      snapshot.proceduralMaterialEnabled = *enabled;
    }
  }
  if (m_materialCallbacks.presets) {
    snapshot.materialPresets = m_materialCallbacks.presets();
  }
  if (m_materialCallbacks.materialParameters) {
    snapshot.materialParameters =
        m_materialCallbacks.materialParameters(snapshot.path);
  }
  return snapshot;
}

CommandResult InspectorPanel::dispatchRename(std::string_view path,
                                             std::string_view newName) {
  const std::string trimmed = trim(newName);
  if (trimmed.empty()) {
    return CommandResult{false, "empty name", {}};
  }
  for (const char c : trimmed) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool allowed = std::isalnum(uc) != 0 || c == "_"[0] || c == "-"[0];
    if (!allowed) {
      return CommandResult{
          false, "invalid name: only [A-Za-z0-9_-] allowed", {}};
    }
  }
  return m_commandBus.dispatch("set " +
                               quoteToken(std::string(path) + ".name") + " " +
                               quoteToken(trimmed));
}

CommandResult InspectorPanel::dispatchSetVec3(std::string_view path,
                                              std::string_view field,
                                              const Vec3f &value) {
  return m_commandBus.dispatch(
      "set " + quoteToken(std::string(path) + "." + std::string(field)) + " " +
      formatFloat(value.x) + " " + formatFloat(value.y) + " " +
      formatFloat(value.z));
}

CommandResult InspectorPanel::dispatchSetFloat(std::string_view path,
                                               std::string_view field,
                                               const float value) {
  return m_commandBus.dispatch(
      "set " + quoteToken(std::string(path) + "." + std::string(field)) + " " +
      formatFloat(value));
}

CommandResult InspectorPanel::dispatchSetUnsigned(std::string_view path,
                                                  std::string_view field,
                                                  const u32 value) {
  return m_commandBus.dispatch(
      "set " + quoteToken(std::string(path) + "." + std::string(field)) + " " +
      formatUnsigned(value));
}

CommandResult InspectorPanel::dispatchSetToken(std::string_view path,
                                               std::string_view field,
                                               std::string_view value) {
  return m_commandBus.dispatch(
      "set " + quoteToken(std::string(path) + "." + std::string(field)) + " " +
      quoteToken(value));
}

CommandResult
InspectorPanel::dispatchApplyMaterialOverride(std::string_view path,
                                              std::string_view field) {
  return m_commandBus.dispatch("apply_material_override " + quoteToken(path) +
                               " " + quoteToken(field));
}

std::vector<std::string> InspectorPanel::discoverExperimentMaterialCandidates(
    const std::filesystem::path &materialsDir) {
  std::vector<std::string> out;
  std::error_code error;
  if (!std::filesystem::exists(materialsDir, error)) {
    return out;
  }
  for (const auto &entry :
       std::filesystem::directory_iterator(materialsDir, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    const std::string filename = path.filename().string();
    if (path.extension() == ".material" && filename.rfind("rtr_", 0) == 0) {
      out.push_back(path.generic_string());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

CommandResult InspectorPanel::dispatchMove(std::string_view path,
                                           const Vec3f &translation) {
  return dispatchSetVec3(path, "translation", translation);
}

CommandResult
InspectorPanel::dispatchRotate(std::string_view path,
                               const Vec3f &rotationEulerDegrees) {
  return dispatchSetVec3(path, "rotation", rotationEulerDegrees);
}

CommandResult InspectorPanel::dispatchScale(std::string_view path,
                                            const Vec3f &scale) {
  return dispatchSetVec3(path, "scale", scale);
}

void InspectorPanel::refreshSceneSubscription() {
  const auto selected = m_editorState.getPrimarySelected();
  const auto selectedScene =
      selected.has_value() ? selected->get().getAttachedScene() : nullptr;
  const auto currentScene = m_subscribedScene.lock();
  if (currentScene.get() == selectedScene.get()) {
    return;
  }

  m_sceneSubscription.reset();
  m_subscribedScene.reset();
  m_snapshotDirty = true;
  if (!selectedScene) {
    return;
  }

  m_subscribedScene = selectedScene;
  m_sceneSubscription = selectedScene->events().subscribe(
      [this](const SceneEvent &event) { handleSceneEvent(event); });
}

void InspectorPanel::handleSceneEvent(const SceneEvent &event) {
  if (shouldInvalidateForEvent(event)) {
    m_snapshotDirty = true;
  }
}

bool InspectorPanel::shouldInvalidateForEvent(const SceneEvent &event) const {
  if (event.domain != SceneEventDomain::Runtime ||
      event.type != SceneEventType::SceneNodeChanged) {
    return false;
  }

  const auto selected = m_editorState.getPrimarySelected();
  if (!selected.has_value()) {
    return false;
  }
  if (event.stableNodeName != selected->get().getNodeName()) {
    return false;
  }

  for (const auto aspect : event.aspects) {
    if (aspect == SceneNodeAspect::Transform ||
        aspect == SceneNodeAspect::Identity ||
        aspect == SceneNodeAspect::Hierarchy ||
        aspect == SceneNodeAspect::Visibility ||
        aspect == SceneNodeAspect::RenderableStructure ||
        aspect == SceneNodeAspect::CameraProperties ||
        aspect == SceneNodeAspect::LightProperties) {
      return true;
    }
  }
  return false;
}

void InspectorPanel::syncDraftFromSnapshot(const Snapshot &snapshot) {
  m_syncedSelectionPath = snapshot.path;
  std::fill(m_nameBuffer.begin(), m_nameBuffer.end(), '\0');
  copyToBuffer(snapshot.name, m_nameBuffer);
  m_translationDraft = snapshot.translation;
  m_rotationDraft = snapshot.rotationEulerDegrees;
  m_scaleDraft = snapshot.scale;
  m_visibilityMaskDraft = snapshot.visibilityMask;
  m_cameraFovDraft = snapshot.cameraFov;
  m_cameraNearDraft = snapshot.cameraNear;
  m_cameraFarDraft = snapshot.cameraFar;
  m_cameraProjectionDraft = snapshot.cameraPerspective ? 0 : 1;
  m_cameraCullingMaskDraft = snapshot.cameraCullingMask;
  m_lightDirectionDraft = snapshot.lightDirection;
  m_lightColorDraft = snapshot.lightColor;
  m_lightIntensityDraft = snapshot.lightIntensity;
  m_lightShadowStrengthDraft = snapshot.lightShadowStrength;
  m_lightShadowBiasDraft = snapshot.lightShadowBias;
  m_lightShadowDistanceDraft = snapshot.lightShadowDistance;
  m_lightShadowCascadeCountDraft = snapshot.lightShadowCascadeCount;
  copyToBuffer(formatMask(snapshot.visibilityMask), m_visibilityMaskBuffer);
  copyToBuffer(formatMask(snapshot.cameraCullingMask),
               m_cameraCullingMaskBuffer);
  copyToBuffer(snapshot.materialUri, m_materialUriBuffer);
  m_nodeBaseColorDraft = snapshot.nodeBaseColorOverride;
  m_materialPresetDraft = -1;
  for (usize i = 0; i < snapshot.materialPresets.size(); ++i) {
    if (snapshot.materialPresets[i] == snapshot.materialUri) {
      m_materialPresetDraft = static_cast<int>(i);
      break;
    }
  }
  m_lightRangeDraft = snapshot.lightRange;
  m_lightInnerConeDraft = snapshot.lightInnerConeDegrees;
  m_lightOuterConeDraft = snapshot.lightOuterConeDegrees;
  m_snapshotDirty = false;
}

void InspectorPanel::drawSelection(const Snapshot &snapshot) {
  auto refreshDrafts = [this]() {
    const Snapshot refreshed = makeSnapshot();
    if (refreshed.hasSelection) {
      syncDraftFromSnapshot(refreshed);
    }
  };
  auto drawMaskEditor = [&](const char *label, std::array<char, 256> &buffer,
                            auto &&dispatchFn) {
    ImGui::PushID(label);
    ImGui::InputText(label, buffer.data(), buffer.size(),
                     ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (const auto value = parseUnsignedText(buffer.data())) {
        const CommandResult result = dispatchFn(*value);
        if (result.ok) {
          refreshDrafts();
        }
      }
    }
    ImGui::PopID();
  };

  ImGui::Text("Path: %s", snapshot.path.c_str());
  ImGui::Separator();

  ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size());
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result =
        dispatchRename(snapshot.path, m_nameBuffer.data());
    if (result.ok) {
      refreshDrafts();
    }
  }

  ImGui::Text("Camera: %s", snapshot.hasCamera ? "yes" : "no");
  ImGui::SameLine();
  ImGui::Text("Light: %s", snapshot.hasLight ? "yes" : "no");
  ImGui::Text("Visibility mask: %s",
              formatMask(snapshot.visibilityMask).c_str());
  ImGui::Text("Mesh: %s", snapshot.hasMesh ? "yes" : "no");
  ImGui::Text("Material: %s", snapshot.hasMaterial ? "yes" : "no");
  ImGui::Text("Skeleton: %s", snapshot.hasSkeleton ? "yes" : "no");
  ImGui::Separator();

  ImGui::DragFloat3("Translation", m_translationDraft.data, 0.1f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result =
        dispatchMove(snapshot.path, m_translationDraft);
    if (result.ok) {
      refreshDrafts();
    }
  }

  ImGui::DragFloat3("Rotation", m_rotationDraft.data, 0.5f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchRotate(snapshot.path, m_rotationDraft);
    if (result.ok) {
      refreshDrafts();
    }
  }

  ImGui::DragFloat3("Scale", m_scaleDraft.data, 0.05f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchScale(snapshot.path, m_scaleDraft);
    if (result.ok) {
      refreshDrafts();
    }
  }

  drawMaskEditor(
      "Visibility Mask", m_visibilityMaskBuffer, [&](const u32 value) {
        return dispatchSetUnsigned(snapshot.path, "visibilityMask", value);
      });

  if (snapshot.hasMaterialSection) {
    ImGui::Separator();
    ImGui::TextUnformatted("Material");

    ImGui::InputText("Material URI", m_materialUriBuffer.data(),
                     m_materialUriBuffer.size(),
                     ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result = dispatchSetToken(
          snapshot.path, "materialUri", m_materialUriBuffer.data());
      if (result.ok) {
        refreshDrafts();
      }
    }

    if (!snapshot.materialPresets.empty()) {
      if (ImGui::Combo(
              "Preset", &m_materialPresetDraft,
              [](void *data, int index, const char **outText) {
                const auto &items =
                    *static_cast<const std::vector<std::string> *>(data);
                if (index < 0 || static_cast<usize>(index) >= items.size()) {
                  return false;
                }
                *outText = items[static_cast<usize>(index)].c_str();
                return true;
              },
              const_cast<std::vector<std::string> *>(&snapshot.materialPresets),
              static_cast<int>(snapshot.materialPresets.size()))) {
        if (m_materialPresetDraft >= 0 &&
            static_cast<usize>(m_materialPresetDraft) <
                snapshot.materialPresets.size()) {
          const CommandResult result = dispatchSetToken(
              snapshot.path, "materialUri",
              snapshot
                  .materialPresets[static_cast<usize>(m_materialPresetDraft)]);
          if (result.ok) {
            refreshDrafts();
          }
        }
      }
    }

    if (snapshot.hasProceduralMaterialEnabled) {
      bool enabled = snapshot.proceduralMaterialEnabled;
      if (ImGui::Checkbox("Procedural Runtime", &enabled)) {
        const CommandResult result = m_commandBus.dispatch(
            "set " + quoteToken(snapshot.path + ".proceduralMaterial.enabled") +
            " " + std::string(enabled ? "true" : "false"));
        if (result.ok) {
          refreshDrafts();
        }
      }
    }

    if (snapshot.canEditBaseColor) {
      const bool baseColorChanged =
          ImGui::ColorEdit3("Base Color Override", m_nodeBaseColorDraft.data);
      if (baseColorChanged) {
        (void)dispatchSetVec3(snapshot.path, "nodeMaterial.baseColor",
                              m_nodeBaseColorDraft);
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        refreshDrafts();
      }
      ImGui::TextUnformatted(
          "Apply Override To Material updates this scene document only.");
      if (ImGui::Button("Apply Override To Material")) {
        const CommandResult result =
            dispatchApplyMaterialOverride(snapshot.path, "baseColor");
        if (result.ok) {
          refreshDrafts();
        }
      }
    } else {
      ImGui::TextUnformatted("Material does not expose MaterialUBO.baseColor.");
    }

    for (const auto &parameter : snapshot.materialParameters) {
      if (parameter.binding == "MaterialUBO" &&
          parameter.member == "baseColor") {
        continue;
      }
      const std::string label = parameter.binding + "." + parameter.member;
      MaterialParameterValue value = parameter.value;
      bool changed = false;
      if (parameter.runtimeOwned) {
        ImGui::BeginDisabled();
      }
      switch (value.type) {
      case MaterialParameterValueType::Float:
        changed = ImGui::InputFloat(
            (parameter.runtimeOwned ? label + " [runtime]" : label).c_str(),
            &value.floatValue);
        break;
      case MaterialParameterValueType::Int:
        changed = ImGui::InputInt(
            (parameter.runtimeOwned ? label + " [runtime]" : label).c_str(),
            &value.intValue);
        break;
      case MaterialParameterValueType::Vec3: {
        float data[3] = {value.vectorValue.x, value.vectorValue.y,
                         value.vectorValue.z};
        changed = ImGui::DragFloat3(
            (parameter.runtimeOwned ? label + " [runtime]" : label).c_str(),
            data, 0.01f);
        value.vectorValue = Vec4f{data[0], data[1], data[2], 0.0f};
        break;
      }
      case MaterialParameterValueType::Vec4: {
        float data[4] = {value.vectorValue.x, value.vectorValue.y,
                         value.vectorValue.z, value.vectorValue.w};
        changed = ImGui::DragFloat4(
            (parameter.runtimeOwned ? label + " [runtime]" : label).c_str(),
            data, 0.01f);
        value.vectorValue = Vec4f{data[0], data[1], data[2], data[3]};
        break;
      }
      }
      if (parameter.runtimeOwned) {
        ImGui::EndDisabled();
      }
      if (changed && ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = m_commandBus.dispatch(
            "set " + quoteToken(snapshot.path + ".nodeMaterial." + label) +
            " " + formatMaterialParameterCommandArgs(value));
        if (result.ok) {
          refreshDrafts();
        }
      }
    }
  }

  if (snapshot.hasCamera) {
    ImGui::Separator();
    ImGui::TextUnformatted("Camera");

    ImGui::DragFloat("FOV", &m_cameraFovDraft, 0.25f, 1.0f, 179.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetFloat(snapshot.path, "fov", m_cameraFovDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    ImGui::DragFloat("Near", &m_cameraNearDraft, 0.01f, 0.001f, 1000.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetFloat(snapshot.path, "near", m_cameraNearDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    ImGui::DragFloat("Far", &m_cameraFarDraft, 1.0f, 0.01f, 10000.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetFloat(snapshot.path, "far", m_cameraFarDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    const char *projectionItems[] = {"Perspective", "Orthographic"};
    if (ImGui::Combo("Projection", &m_cameraProjectionDraft, projectionItems,
                     IM_ARRAYSIZE(projectionItems))) {
      const CommandResult result = dispatchSetToken(
          snapshot.path, "projection",
          m_cameraProjectionDraft == 0 ? "perspective" : "orthographic");
      if (result.ok) {
        refreshDrafts();
      }
    }

    drawMaskEditor(
        "Camera Culling Mask", m_cameraCullingMaskBuffer, [&](const u32 value) {
          return dispatchSetUnsigned(snapshot.path, "cullingMask", value);
        });
  }

  if (snapshot.hasLight) {
    ImGui::Separator();
    ImGui::Text("%s Light", snapshot.lightKind.c_str());

    if (snapshot.lightKind == "Directional" || snapshot.lightKind == "Spot") {
      ImGui::DragFloat3("Direction", m_lightDirectionDraft.data, 0.05f);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = dispatchSetVec3(
            snapshot.path, "light.direction", m_lightDirectionDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }
    }

    ImGui::ColorEdit3("Color", m_lightColorDraft.data);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetVec3(snapshot.path, "light.color", m_lightColorDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    ImGui::DragFloat("Intensity", &m_lightIntensityDraft, 0.05f, 0.0f, 100.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result = dispatchSetFloat(
          snapshot.path, "light.intensity", m_lightIntensityDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    if (snapshot.lightKind == "Directional") {
      ImGui::DragFloat("Shadow Strength", &m_lightShadowStrengthDraft, 0.01f,
                       0.0f, 1.0f);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = dispatchSetFloat(
            snapshot.path, "light.shadowStrength", m_lightShadowStrengthDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }

      ImGui::DragFloat("Shadow Bias", &m_lightShadowBiasDraft, 0.001f, 0.0f,
                       10.0f, "%.4f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = dispatchSetFloat(
            snapshot.path, "light.shadowBias", m_lightShadowBiasDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }

      ImGui::DragFloat("Shadow Distance", &m_lightShadowDistanceDraft, 1.0f,
                       1.0f, 1000.0f);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = dispatchSetFloat(
            snapshot.path, "light.shadowDistance", m_lightShadowDistanceDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }

      int cascadeCountDraft = static_cast<int>(m_lightShadowCascadeCountDraft);
      ImGui::SliderInt("Shadow Cascades", &cascadeCountDraft, 1,
                       static_cast<int>(MaxShadowCascades));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        m_lightShadowCascadeCountDraft = static_cast<u32>(std::clamp(
            cascadeCountDraft, 1, static_cast<int>(MaxShadowCascades)));
        const CommandResult result =
            dispatchSetUnsigned(snapshot.path, "light.shadowCascadeCount",
                                m_lightShadowCascadeCountDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }
    }

    if (snapshot.lightKind == "Point" || snapshot.lightKind == "Spot") {
      ImGui::DragFloat("Range", &m_lightRangeDraft, 0.1f, 0.0f, 1000.0f);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result =
            dispatchSetFloat(snapshot.path, "light.range", m_lightRangeDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }
    }

    if (snapshot.lightKind == "Spot") {
      ImGui::DragFloat("Inner Cone", &m_lightInnerConeDraft, 0.25f, 0.0f,
                       179.0f);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = dispatchSetFloat(
            snapshot.path, "light.innerConeDegrees", m_lightInnerConeDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }
      ImGui::DragFloat("Outer Cone", &m_lightOuterConeDraft, 0.25f, 0.0f,
                       179.0f);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const CommandResult result = dispatchSetFloat(
            snapshot.path, "light.outerConeDegrees", m_lightOuterConeDraft);
        if (result.ok) {
          refreshDrafts();
        }
      }
    }
  }
}

} // namespace LX_core
