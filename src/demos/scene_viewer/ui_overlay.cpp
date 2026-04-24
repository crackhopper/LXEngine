#include "ui_overlay.hpp"

#include "camera_rig.hpp"

#include "core/input/key_code.hpp"
#include "infra/gui/debug_ui.hpp"

#include <imgui.h>

namespace LX_demo::scene_viewer {

namespace dui = LX_infra::debug_ui;

void UiOverlay::attach(LX_core::Camera& camera,
                       LX_core::DirectionalLight& light,
                       CameraRig& rig) {
  m_camera = std::ref(camera);
  m_light = std::ref(light);
  m_rig = std::ref(rig);
}

void UiOverlay::attachClock(const LX_core::Clock& clock) {
  m_clock = std::cref(clock);
}

void UiOverlay::handleHotkeys(LX_core::IInputState& input) {
  const bool f1Down = input.isKeyDown(LX_core::KeyCode::F1);
  if (f1Down && !m_prevF1Down) {
    m_helpVisible = !m_helpVisible;
  }
  m_prevF1Down = f1Down;
}

void UiOverlay::drawFrame() {
  if (dui::beginPanel("Stats")) {
    if (m_clock) {
      dui::renderStatsPanel(m_clock->get());
    }
    if (m_rig) {
      const bool orbit = m_rig->get().currentMode() == CameraRig::Mode::Orbit;
      dui::labelText("camera mode", orbit ? "Orbit" : "FreeFly");
    }
  }
  dui::endPanel();

  if (dui::beginPanel("Camera")) {
    if (m_camera) {
      dui::cameraPanel("Camera", m_camera->get());
      // The rig calls updateMatrices() every frame; UI edits land naturally.
    }
  }
  dui::endPanel();

  if (dui::beginPanel("Directional Light")) {
    if (m_light) {
      dui::directionalLightPanel("Sun", m_light->get());
    }
  }
  dui::endPanel();

  if (m_helpVisible) {
    if (dui::beginPanel("Help")) {
      ImGui::TextUnformatted("F1  toggle this help panel");
      ImGui::TextUnformatted("F2  switch Orbit / FreeFly");
      ImGui::Separator();
      ImGui::TextUnformatted("Orbit:");
      ImGui::BulletText("left-drag   rotate");
      ImGui::BulletText("right-drag  pan target");
      ImGui::BulletText("wheel       zoom");
      ImGui::Separator();
      ImGui::TextUnformatted("FreeFly:");
      ImGui::BulletText("right-hold  look around");
      ImGui::BulletText("W/A/S/D     translate");
      ImGui::BulletText("Space       ascend");
      ImGui::BulletText("LShift      descend");
      ImGui::BulletText("LCtrl       accelerate");
    }
    dui::endPanel();
  }
}

} // namespace LX_demo::scene_viewer
