#include "ui_overlay.hpp"

#include "camera_rig.hpp"

#include "core/editor/command_bus.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "core/editor/viewport_overlay.hpp"
#include "core/input/key_code.hpp"
#include "infra/gui/debug_ui.hpp"

#include <imgui.h>

namespace LX_demo::scene_viewer {

namespace dui = LX_infra::debug_ui;
namespace {

[[nodiscard]] std::string quoteToken(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

} // namespace

void UiOverlay::attach(CameraRig& rig, LX_core::CommandBus& commandBus,
                       LX_core::SceneTreePanel& sceneTreePanel,
                       LX_core::InspectorPanel& inspectorPanel,
                       LX_core::ConsolePanel& consolePanel,
                       LX_core::ViewportOverlay& viewportOverlay) {
  m_rig = std::ref(rig);
  m_commandBus = std::ref(commandBus);
  m_sceneTreePanel = std::ref(sceneTreePanel);
  m_inspectorPanel = std::ref(inspectorPanel);
  m_consolePanel = std::ref(consolePanel);
  m_viewportOverlay = std::ref(viewportOverlay);
}

void UiOverlay::attachClock(const LX_core::Clock& clock) { m_clock = std::cref(clock); }

void UiOverlay::setDefaultLayoutEnabled(const bool enabled) {
  m_defaultLayoutEnabled = enabled;
  if (enabled) {
    m_defaultLayoutApplied = false;
  }
}

void UiOverlay::applyDefaultLayout() {
  if (m_defaultLayoutEnabled) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool displayChanged = !m_defaultLayoutApplied ||
                                display.x != m_lastDisplaySize.x ||
                                display.y != m_lastDisplaySize.y;
    const float leftWidth = 260.0f;
    const float rightWidth = 340.0f;
    const float bottomHeight = 220.0f;
    const float topInset = 72.0f;
    const float centerWidth =
        std::max(1.0f, display.x - leftWidth - rightWidth);
    const float centerHeight = std::max(1.0f, display.y - bottomHeight);

    const ImGuiCond layoutCond =
        displayChanged ? ImGuiCond_Always : ImGuiCond_Once;

    ImGui::SetNextWindowPos(ImVec2(0.0f, topInset), layoutCond);
    ImGui::SetNextWindowSize(ImVec2(leftWidth, centerHeight - topInset),
                             layoutCond);
    if (m_sceneTreePanel) {
      m_sceneTreePanel->get().draw();
    }

    ImGui::SetNextWindowPos(ImVec2(display.x - rightWidth, 0.0f), layoutCond);
    ImGui::SetNextWindowSize(ImVec2(rightWidth, centerHeight), layoutCond);
    if (m_inspectorPanel) {
      m_inspectorPanel->get().draw();
    }

    ImGui::SetNextWindowPos(ImVec2(leftWidth, centerHeight), layoutCond);
    ImGui::SetNextWindowSize(ImVec2(centerWidth, bottomHeight), layoutCond);
    if (m_consolePanel) {
      m_consolePanel->get().draw();
    }

    ImGui::SetNextWindowPos(ImVec2(leftWidth, 0.0f), layoutCond);
    ImGui::SetNextWindowSize(ImVec2(centerWidth, centerHeight), layoutCond);
    if (m_viewportOverlay) {
      m_viewportOverlay->get().draw();
    }

    m_defaultLayoutApplied = true;
    m_lastDisplaySize = display;
    return;
  }

  if (m_sceneTreePanel) {
    m_sceneTreePanel->get().draw();
  }
  if (m_inspectorPanel) {
    m_inspectorPanel->get().draw();
  }
  if (m_consolePanel) {
    m_consolePanel->get().draw();
  }
  if (m_viewportOverlay) {
    m_viewportOverlay->get().draw();
  }
}

void UiOverlay::handleHotkeys(LX_core::IInputState& input) {
  const bool f1Down = input.isKeyDown(LX_core::KeyCode::F1);
  if (f1Down && !m_prevF1Down) {
    m_helpVisible = !m_helpVisible;
  }
  m_prevF1Down = f1Down;

  const bool fDown = input.isKeyDown(LX_core::KeyCode::F);
  if (fDown && !m_prevFDown && m_viewportOverlay) {
    (void)m_viewportOverlay->get().dispatchPreviewToggle();
  }
  m_prevFDown = fDown;

  const bool wDown = input.isKeyDown(LX_core::KeyCode::W);
  if (wDown && !m_prevWDown && m_viewportOverlay) {
    (void)m_viewportOverlay->get().handleGizmoHotkeys('W');
  }
  m_prevWDown = wDown;

  const bool eDown = input.isKeyDown(LX_core::KeyCode::E);
  if (eDown && !m_prevEDown && m_viewportOverlay) {
    (void)m_viewportOverlay->get().handleGizmoHotkeys('E');
  }
  m_prevEDown = eDown;

  const bool rDown = input.isKeyDown(LX_core::KeyCode::R);
  if (rDown && !m_prevRDown && m_viewportOverlay) {
    (void)m_viewportOverlay->get().handleGizmoHotkeys('R');
  }
  m_prevRDown = rDown;

  const bool escapeDown = input.isKeyDown(LX_core::KeyCode::Escape);
  if (escapeDown && !m_prevEscapeDown && m_commandBus) {
    (void)m_commandBus->get().dispatch("deselect");
  }
  m_prevEscapeDown = escapeDown;

  const bool deleteDown = input.isKeyDown(LX_core::KeyCode::Delete);
  if (deleteDown && !m_prevDeleteDown && m_commandBus && m_viewportOverlay) {
    const auto snapshot = m_viewportOverlay->get().makeSnapshot();
    if (!snapshot.selectedPath.empty()) {
      (void)m_commandBus->get().dispatch("remove " + quoteToken(snapshot.selectedPath));
    }
  }
  m_prevDeleteDown = deleteDown;
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

  applyDefaultLayout();

  if (m_helpVisible) {
    if (dui::beginPanel("Help")) {
      ImGui::TextUnformatted("F1  toggle this help panel");
      ImGui::TextUnformatted("F   preview toggle (same command path as console)");
      ImGui::TextUnformatted("F2  switch Orbit / FreeFly");
      ImGui::TextUnformatted("W/E/R gizmo mode | Esc deselect | Delete remove");
      ImGui::TextUnformatted("Scene Tree / Inspector / Console share one EditorState");
      ImGui::TextUnformatted("Inspector fields commit through command bus");
    }
    dui::endPanel();
  }
}

} // namespace LX_demo::scene_viewer
