#include "debug_ui.hpp"

#include <imgui.h>

#include <cstdio>

namespace LX_infra::debug_ui {

// Catch math-type layout drift at compile time — helpers bridge by passing
// &value.data[0] straight into ImGui's float* widgets, so any change that
// adds padding or reorders the union will corrupt UI edits silently without
// these asserts.
static_assert(sizeof(LX_core::Vec3f) == 3 * sizeof(float),
              "Vec3f must be tightly packed as 3 floats for debug_ui");
static_assert(sizeof(LX_core::Vec4f) == 4 * sizeof(float),
              "Vec4f must be tightly packed as 4 floats for debug_ui");

// ---- Vector / scalar bridging -------------------------------------------

bool dragVec3(const char* label, LX_core::Vec3f& value,
              float speed, float min, float max) {
  return ImGui::DragFloat3(label, value.data, speed, min, max);
}

bool dragVec4(const char* label, LX_core::Vec4f& value,
              float speed, float min, float max) {
  return ImGui::DragFloat4(label, value.data, speed, min, max);
}

bool sliderFloat(const char* label, float& value, float min, float max) {
  return ImGui::SliderFloat(label, &value, min, max);
}

bool sliderInt(const char* label, int& value, int min, int max) {
  return ImGui::SliderInt(label, &value, min, max);
}

bool colorEdit3(const char* label, LX_core::Vec3f& rgb) {
  return ImGui::ColorEdit3(label, rgb.data);
}

bool colorEdit4(const char* label, LX_core::Vec4f& rgba) {
  return ImGui::ColorEdit4(label, rgba.data);
}

// ---- Label / display helpers --------------------------------------------

void labelText(const char* label, const char* value) {
  ImGui::LabelText(label, "%s", value ? value : "");
}

void labelText(const char* label, const std::string& value) {
  ImGui::LabelText(label, "%s", value.c_str());
}

void labelFloat(const char* label, float value) {
  ImGui::LabelText(label, "%.3f", value);
}

void labelInt(const char* label, int value) {
  ImGui::LabelText(label, "%d", value);
}

void labelStringId(const char* label, LX_core::StringID value) {
  const auto& name = LX_core::GlobalStringTable::get().getName(value.id);
  if (name.empty()) {
    // Fallback so callers see something actionable instead of a blank row
    // when an ID was never registered with the global table.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "(empty #%u)", value.id);
    ImGui::LabelText(label, "%s", buf);
  } else {
    ImGui::LabelText(label, "%s", name.c_str());
  }
}

// ---- Panel / section containers -----------------------------------------

bool beginPanel(const char* title) {
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 400.0f), ImGuiCond_FirstUseEver);
  return ImGui::Begin(title);
}

void endPanel() {
  // ImGui requires End() to match every Begin() regardless of its return value.
  ImGui::End();
}

bool beginSection(const char* title) {
  return ImGui::CollapsingHeader(title);
}

void endSection() {
  // CollapsingHeader has no close call; this stays a no-op to preserve the
  // symmetric begin/end API so future switches to TreeNode/TreePop don't
  // churn every call site.
}

void separatorText(const char* label) {
  ImGui::SeparatorText(label);
}

// ---- Composite panels ---------------------------------------------------

void renderStatsPanel(const LX_core::Clock& clock) {
  separatorText("Frame");
  labelInt("frame", static_cast<int>(clock.frameCount()));
  labelFloat("dt (ms)", clock.deltaTime() * 1000.0f);
  const float smoothed = clock.smoothedDeltaTime();
  const float fps = smoothed > 0.0f ? 1.0f / smoothed : 0.0f;
  labelFloat("fps", fps);
}

void cameraPanel(const char* title, LX_core::CameraComponent& camera) {
  separatorText(title);
  auto eye = camera.getEyePosition();
  auto target = camera.getLookTarget();
  auto up = camera.getUpVector();

  bool poseChanged = false;
  poseChanged |= dragVec3("position", eye, 0.05f);
  poseChanged |= dragVec3("target", target, 0.05f);
  poseChanged |= dragVec3("up", up, 0.01f);
  if (poseChanged) {
    camera.lookAt(eye, target, up);
  }

  float fovY = camera.getFovY();
  if (sliderFloat("fovY", fovY, 1.0f, 179.0f)) {
    camera.setFovY(fovY);
  }

  float aspect = camera.getAspect();
  if (sliderFloat("aspect", aspect, 0.1f, 4.0f)) {
    camera.setAspect(aspect);
  }

  float nearPlane = camera.getNearPlane();
  if (sliderFloat("near", nearPlane, 0.001f, 10.0f)) {
    camera.setNearPlane(nearPlane);
  }

  float farPlane = camera.getFarPlane();
  if (sliderFloat("far", farPlane, 1.0f, 10000.0f)) {
    camera.setFarPlane(farPlane);
  }
}

void directionalLightPanel(const char* title,
                           LX_core::DirectionalLight& light) {
  separatorText(title);
  bool changed = false;
  changed |= dragVec4("dir", light.ubo->param.dir, 0.01f);
  changed |= colorEdit4("color", light.ubo->param.color);
  if (changed) {
    light.ubo->setDirty();
  }
}

} // namespace LX_infra::debug_ui
