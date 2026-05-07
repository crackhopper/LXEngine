#include "core/editor/inspector_panel.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <imgui.h>
#include <sstream>

namespace LX_core {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;

[[nodiscard]] std::string trim(std::string_view text) {
  usize begin = 0;
  while (begin < text.size() &&
         (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
          text[begin] == '\n')) {
    ++begin;
  }

  usize end = text.size();
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' ||
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

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return text;
}

[[nodiscard]] std::shared_ptr<DirectionalLight>
findDirectionalLightForNode(const SceneNode &node) {
  const auto scene = node.getAttachedScene();
  if (!scene) {
    return nullptr;
  }

  const std::string tag = lowerCopy(node.getName() + " " + node.getPath());
  if (tag.find("light") == std::string::npos) {
    return nullptr;
  }

  for (const auto &light : scene->getLights()) {
    const auto directionalLight = std::dynamic_pointer_cast<DirectionalLight>(light);
    if (directionalLight && directionalLight->ubo) {
      return directionalLight;
    }
  }
  return nullptr;
}

[[nodiscard]] Vec3f quatToEulerDegrees(const Quatf &quat) {
  const Quatf q = quat.normalized();

  const float sinrCosp = 2.0f * (q.w * q.v.x + q.v.y * q.v.z);
  const float cosrCosp = 1.0f - 2.0f * (q.v.x * q.v.x + q.v.y * q.v.y);
  const float roll = std::atan2(sinrCosp, cosrCosp);

  const float sinp = 2.0f * (q.w * q.v.y - q.v.z * q.v.x);
  const float pitch = std::abs(sinp) >= 1.0f
                          ? std::copysign(0.5f * kPi, sinp)
                          : std::asin(sinp);

  const float sinyCosp = 2.0f * (q.w * q.v.z + q.v.x * q.v.y);
  const float cosyCosp = 1.0f - 2.0f * (q.v.y * q.v.y + q.v.z * q.v.z);
  const float yaw = std::atan2(sinyCosp, cosyCosp);

  return Vec3f{roll * kRadToDeg, pitch * kRadToDeg, yaw * kRadToDeg};
}

} // namespace

InspectorPanel::InspectorPanel(CommandBus &commandBus, EditorState &editorState)
    : m_commandBus(commandBus), m_editorState(editorState) {}

void InspectorPanel::draw() {
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
    ImGui::End();
    return;
  }

  if (snapshot.path != m_syncedSelectionPath) {
    syncDraftFromSnapshot(snapshot);
  }

  drawSelection(snapshot);
  ImGui::End();
}

InspectorPanel::Snapshot InspectorPanel::makeSnapshot() const {
  Snapshot snapshot;
  const auto selected = m_editorState.getSelected();
  if (!selected) {
    return snapshot;
  }

  snapshot.hasSelection = true;
  snapshot.path = selected->getPath();
  snapshot.name = selected->getName();
  snapshot.translation = selected->getTranslation();
  snapshot.rotationEulerDegrees = quatToEulerDegrees(selected->getRotation());
  snapshot.scale = selected->getScale();
  snapshot.visibilityMask = selected->getVisibilityLayerMask();
  snapshot.hasCamera = selected->getComponent<CameraComponent>().has_value();
  if (snapshot.hasCamera) {
    const auto camera = selected->getComponent<CameraComponent>();
    snapshot.cameraFov = camera->get().fovY;
    snapshot.cameraNear = camera->get().nearPlane;
    snapshot.cameraFar = camera->get().farPlane;
    snapshot.cameraPerspective = camera->get().type == CameraType::Perspective;
    snapshot.cameraCullingMask = camera->get().getCullingMask();
  }
  if (const auto light = findDirectionalLightForNode(*selected)) {
    snapshot.hasLight = true;
    snapshot.lightDirection = Vec3f{light->ubo->param.dir.x, light->ubo->param.dir.y, light->ubo->param.dir.z};
    snapshot.lightColor = Vec3f{light->ubo->param.color.x, light->ubo->param.color.y, light->ubo->param.color.z};
    snapshot.lightIntensity = light->ubo->param.color.w;
  }
  snapshot.hasMesh = selected->getComponent<MeshComponent>().has_value();
  snapshot.hasMaterial = selected->getComponent<MaterialComponent>().has_value();
  snapshot.hasSkeleton = selected->getComponent<SkeletonComponent>().has_value();
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
      return CommandResult{false, "invalid name: only [A-Za-z0-9_-] allowed", {}};
    }
  }
  return m_commandBus.dispatch("set " + quoteToken(std::string(path) + ".name") + " " +
                               quoteToken(trimmed));
}

CommandResult InspectorPanel::dispatchSetVec3(std::string_view path,
                                              std::string_view field,
                                              const Vec3f &value) {
  return m_commandBus.dispatch("set " +
                               quoteToken(std::string(path) + "." + std::string(field)) + " " +
                               formatFloat(value.x) + " " +
                               formatFloat(value.y) + " " +
                               formatFloat(value.z));
}

CommandResult InspectorPanel::dispatchSetFloat(std::string_view path,
                                               std::string_view field,
                                               const float value) {
  return m_commandBus.dispatch("set " +
                               quoteToken(std::string(path) + "." + std::string(field)) + " " +
                               formatFloat(value));
}

CommandResult InspectorPanel::dispatchSetUnsigned(std::string_view path,
                                                  std::string_view field,
                                                  const u32 value) {
  return m_commandBus.dispatch("set " +
                               quoteToken(std::string(path) + "." + std::string(field)) + " " +
                               formatUnsigned(value));
}

CommandResult InspectorPanel::dispatchSetToken(std::string_view path,
                                               std::string_view field,
                                               std::string_view value) {
  return m_commandBus.dispatch("set " +
                               quoteToken(std::string(path) + "." + std::string(field)) + " " +
                               quoteToken(value));
}

CommandResult InspectorPanel::dispatchMove(std::string_view path,
                                           const Vec3f &translation) {
  return dispatchSetVec3(path, "translation", translation);
}

CommandResult InspectorPanel::dispatchRotate(std::string_view path,
                                             const Vec3f &rotationEulerDegrees) {
  return dispatchSetVec3(path, "rotation", rotationEulerDegrees);
}

CommandResult InspectorPanel::dispatchScale(std::string_view path,
                                            const Vec3f &scale) {
  return dispatchSetVec3(path, "scale", scale);
}

void InspectorPanel::syncDraftFromSnapshot(const Snapshot &snapshot) {
  m_syncedSelectionPath = snapshot.path;
  std::fill(m_nameBuffer.begin(), m_nameBuffer.end(), '\0');
  const usize copyLength = std::min(snapshot.name.size(), m_nameBuffer.size() - 1);
  if (copyLength > 0) {
    std::memcpy(m_nameBuffer.data(), snapshot.name.data(), copyLength);
  }
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
}

void InspectorPanel::drawSelection(const Snapshot &snapshot) {
  auto refreshDrafts = [this]() {
    const Snapshot refreshed = makeSnapshot();
    if (refreshed.hasSelection) {
      syncDraftFromSnapshot(refreshed);
    }
  };
  auto drawMaskEditor = [&](const char *label, u32 &draft,
                            auto &&dispatchFn) {
    ImGui::Text("%s: 0x%08X", label, draft);
    ImGui::PushID(label);
    bool changed = false;
    for (int bit = 0; bit < 32; ++bit) {
      bool enabled = (draft & (1u << bit)) != 0;
      ImGui::PushID(bit);
      if (ImGui::Checkbox("##bit", &enabled)) {
        if (enabled) {
          draft |= (1u << bit);
        } else {
          draft &= ~(1u << bit);
        }
        changed = true;
      }
      ImGui::SameLine();
      ImGui::Text("%02d", bit);
      if ((bit % 4) != 3) {
        ImGui::SameLine();
      }
      ImGui::PopID();
    }
    if (changed) {
      const CommandResult result = dispatchFn(draft);
      if (result.ok) {
        refreshDrafts();
      }
    }
    ImGui::PopID();
  };

  ImGui::Text("Path: %s", snapshot.path.c_str());
  ImGui::Separator();

  ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size());
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchRename(snapshot.path, m_nameBuffer.data());
    if (result.ok) {
      refreshDrafts();
    }
  }

  ImGui::Text("Camera: %s", snapshot.hasCamera ? "yes" : "no");
  ImGui::SameLine();
  ImGui::Text("Light: %s", snapshot.hasLight ? "yes" : "no");
  ImGui::Text("Visibility mask: 0x%08X", snapshot.visibilityMask);
  ImGui::Text("Mesh: %s", snapshot.hasMesh ? "yes" : "no");
  ImGui::Text("Material: %s", snapshot.hasMaterial ? "yes" : "no");
  ImGui::Text("Skeleton: %s", snapshot.hasSkeleton ? "yes" : "no");
  ImGui::Separator();

  ImGui::DragFloat3("Translation", m_translationDraft.data, 0.1f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchMove(snapshot.path, m_translationDraft);
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

  drawMaskEditor("Visibility bits", m_visibilityMaskDraft,
                 [&](const u32 value) {
                   return dispatchSetUnsigned(snapshot.path, "visibilityMask", value);
                 });

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

    drawMaskEditor("Camera culling bits", m_cameraCullingMaskDraft,
                   [&](const u32 value) {
                     return dispatchSetUnsigned(snapshot.path, "cullingMask",
                                                value);
                   });
  }

  if (snapshot.hasLight) {
    ImGui::Separator();
    ImGui::TextUnformatted("Directional Light");

    ImGui::DragFloat3("Direction", m_lightDirectionDraft.data, 0.05f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetVec3(snapshot.path, "direction", m_lightDirectionDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    ImGui::ColorEdit3("Color", m_lightColorDraft.data);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetVec3(snapshot.path, "color", m_lightColorDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }

    ImGui::DragFloat("Intensity", &m_lightIntensityDraft, 0.05f, 0.0f, 100.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const CommandResult result =
          dispatchSetFloat(snapshot.path, "intensity", m_lightIntensityDraft);
      if (result.ok) {
        refreshDrafts();
      }
    }
  }
}

} // namespace LX_core
