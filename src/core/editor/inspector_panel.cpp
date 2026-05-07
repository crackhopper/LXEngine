#include "core/editor/inspector_panel.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/object.hpp"

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
  snapshot.hasCamera = selected->getComponent<CameraComponent>().has_value();
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

CommandResult InspectorPanel::dispatchMove(std::string_view path,
                                           const Vec3f &translation) {
  return m_commandBus.dispatch("move " + quoteToken(path) + " " +
                               formatFloat(translation.x) + " " +
                               formatFloat(translation.y) + " " +
                               formatFloat(translation.z));
}

CommandResult InspectorPanel::dispatchRotate(std::string_view path,
                                             const Vec3f &rotationEulerDegrees) {
  return m_commandBus.dispatch("rotate " + quoteToken(path) + " " +
                               formatFloat(rotationEulerDegrees.x) + " " +
                               formatFloat(rotationEulerDegrees.y) + " " +
                               formatFloat(rotationEulerDegrees.z));
}

CommandResult InspectorPanel::dispatchScale(std::string_view path,
                                            const Vec3f &scale) {
  return m_commandBus.dispatch("scale " + quoteToken(path) + " " +
                               formatFloat(scale.x) + " " +
                               formatFloat(scale.y) + " " +
                               formatFloat(scale.z));
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
}

void InspectorPanel::drawSelection(const Snapshot &snapshot) {
  ImGui::TextUnformatted(snapshot.path.c_str());
  ImGui::Separator();

  ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size());
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchRename(snapshot.path, m_nameBuffer.data());
    if (result.ok) {
      const Snapshot refreshed = makeSnapshot();
      if (refreshed.hasSelection) {
        syncDraftFromSnapshot(refreshed);
      }
    }
  }

  ImGui::Text("Camera: %s", snapshot.hasCamera ? "yes" : "no");
  ImGui::Text("Mesh: %s", snapshot.hasMesh ? "yes" : "no");
  ImGui::Text("Material: %s", snapshot.hasMaterial ? "yes" : "no");
  ImGui::Text("Skeleton: %s", snapshot.hasSkeleton ? "yes" : "no");
  ImGui::TextUnformatted("Light: scene-level only (no SceneNode component in v1)");
  ImGui::Separator();

  ImGui::DragFloat3("Translation", m_translationDraft.data, 0.1f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchMove(snapshot.path, m_translationDraft);
    if (result.ok) {
      m_translationDraft = makeSnapshot().translation;
    }
  }

  ImGui::DragFloat3("Rotation", m_rotationDraft.data, 0.5f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchRotate(snapshot.path, m_rotationDraft);
    if (result.ok) {
      m_rotationDraft = makeSnapshot().rotationEulerDegrees;
    }
  }

  ImGui::DragFloat3("Scale", m_scaleDraft.data, 0.05f);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const CommandResult result = dispatchScale(snapshot.path, m_scaleDraft);
    if (result.ok) {
      m_scaleDraft = makeSnapshot().scale;
    }
  }
}

} // namespace LX_core
