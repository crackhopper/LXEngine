#include "ui_overlay.hpp"

#include "camera_rig.hpp"

#include "core/editor/command_bus.hpp"
#include "core/editor/console_panel.hpp"
#include "core/editor/editor_state.hpp"
#include "core/editor/inspector_panel.hpp"
#include "core/editor/scene_tree_panel.hpp"
#include "infra/gui/debug_ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

namespace dui = LX_infra::debug_ui;
namespace {

constexpr float kMinUiFontScale = 0.75f;
constexpr float kMaxUiFontScale = 2.0f;

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

[[nodiscard]] float clampUiFontScale(const float value) {
  return std::clamp(value, kMinUiFontScale, kMaxUiFontScale);
}

[[nodiscard]] const char* editModeLabel(const UiOverlay::EditMode mode) {
  switch (mode) {
  case UiOverlay::EditMode::Selection:
    return "Selection";
  case UiOverlay::EditMode::Orbit:
    return "Orbit";
  case UiOverlay::EditMode::FreeFly:
    return "FreeFly";
  }
  return "Selection";
}

void drawButtonIcon(ImDrawList& drawList, const ImVec2 min, const ImVec2 max,
                    const UiOverlay::EditMode mode, const ImU32 color) {
  const float w = max.x - min.x;
  const float h = max.y - min.y;
  const ImVec2 c{min.x + w * 0.5f, min.y + h * 0.5f};
  const float pad = std::min(w, h) * 0.18f;
  const float stroke = 2.0f;

  switch (mode) {
  case UiOverlay::EditMode::Selection: {
    const ImVec2 a{min.x + pad, min.y + pad};
    const ImVec2 b{min.x + w * 0.62f, min.y + h * 0.52f};
    const ImVec2 tip{min.x + w * 0.42f, max.y - pad};
    drawList.AddTriangleFilled(a, b, tip, color);
    drawList.AddLine(b, ImVec2(max.x - pad, max.y - pad), color, stroke);
    break;
  }
  case UiOverlay::EditMode::Orbit:
    drawList.AddCircle(c, std::min(w, h) * 0.28f, color, 0, stroke);
    drawList.AddLine(ImVec2(c.x, min.y + pad), ImVec2(c.x, max.y - pad), color,
                     stroke);
    drawList.AddLine(ImVec2(min.x + pad, c.y), ImVec2(max.x - pad, c.y), color,
                     stroke);
    break;
  case UiOverlay::EditMode::FreeFly:
    drawList.AddLine(ImVec2(min.x + pad, c.y), ImVec2(max.x - pad, c.y), color,
                     stroke);
    drawList.AddLine(ImVec2(c.x, min.y + pad), ImVec2(c.x, max.y - pad), color,
                     stroke);
    drawList.AddTriangleFilled(ImVec2(max.x - pad, c.y),
                               ImVec2(max.x - pad - 8.0f, c.y - 5.0f),
                               ImVec2(max.x - pad - 8.0f, c.y + 5.0f), color);
    drawList.AddTriangleFilled(ImVec2(c.x, min.y + pad),
                               ImVec2(c.x - 5.0f, min.y + pad + 8.0f),
                               ImVec2(c.x + 5.0f, min.y + pad + 8.0f), color);
    break;
  }
}

void drawPreviewIcon(ImDrawList& drawList, const ImVec2 min, const ImVec2 max,
                     const ImU32 color) {
  const float pad = std::min(max.x - min.x, max.y - min.y) * 0.18f;
  drawList.AddRect(ImVec2(min.x + pad, min.y + pad),
                   ImVec2(max.x - pad, max.y - pad), color, 4.0f, 0, 2.0f);
  drawList.AddTriangleFilled(ImVec2(min.x + pad + 5.0f, min.y + pad + 5.0f),
                             ImVec2(max.x - pad - 5.0f, (min.y + max.y) * 0.5f),
                             ImVec2(min.x + pad + 5.0f, max.y - pad - 5.0f),
                             color);
}

void drawPreferencesIcon(ImDrawList& drawList, const ImVec2 min,
                         const ImVec2 max, const ImU32 color) {
  const ImVec2 c{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
  const float r = std::min(max.x - min.x, max.y - min.y) * 0.16f;
  drawList.AddCircle(c, r, color, 0, 2.0f);
  for (int i = 0; i < 6; ++i) {
    const float angle = static_cast<float>(i) * 1.04719755f;
    const ImVec2 a{c.x + std::cos(angle) * (r + 3.0f),
                   c.y + std::sin(angle) * (r + 3.0f)};
    const ImVec2 b{c.x + std::cos(angle) * (r + 8.0f),
                   c.y + std::sin(angle) * (r + 8.0f)};
    drawList.AddLine(a, b, color, 2.0f);
  }
}

void drawResetIcon(ImDrawList& drawList, const ImVec2 min, const ImVec2 max,
                   const ImU32 color) {
  const ImVec2 c{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
  const float radius = std::min(max.x - min.x, max.y - min.y) * 0.24f;
  drawList.PathArcTo(c, radius, 0.9f, 5.2f, 20);
  drawList.PathStroke(color, 0, 2.0f);
  drawList.AddTriangleFilled(
      ImVec2(c.x - radius * 0.85f, c.y - radius * 0.45f),
      ImVec2(c.x - radius * 0.25f, c.y - radius * 0.95f),
      ImVec2(c.x - radius * 0.15f, c.y - radius * 0.25f), color);
}

[[nodiscard]] float maxWindowBottom(const char* name, const float fallback) {
  if (ImGuiWindow* window = ImGui::FindWindowByName(name);
      window != nullptr && window->WasActive) {
    return window->Pos.y + window->Size.y;
  }
  return fallback;
}

[[nodiscard]] float maxWindowRight(const char* name, const float fallback) {
  if (ImGuiWindow* window = ImGui::FindWindowByName(name);
      window != nullptr && window->WasActive && !window->Collapsed) {
    return window->Pos.x + window->Size.x;
  }
  return fallback;
}

[[nodiscard]] float rightInsetFromWindow(const char* name,
                                         const float displayWidth,
                                         const float fallback) {
  if (ImGuiWindow* window = ImGui::FindWindowByName(name);
      window != nullptr && window->WasActive && !window->Collapsed) {
    return std::max(0.0f, displayWidth - window->Pos.x);
  }
  return fallback;
}

[[nodiscard]] float bottomInsetFromWindowTop(const char* name,
                                             const float displayHeight,
                                             const float fallback) {
  if (ImGuiWindow* window = ImGui::FindWindowByName(name);
      window != nullptr && window->WasActive && !window->Collapsed) {
    return std::max(0.0f, displayHeight - window->Pos.y);
  }
  return fallback;
}

[[nodiscard]] bool drawIconToggleButton(const char* id, const bool active,
                                        const char* tooltip,
                                        void (*drawIcon)(ImDrawList&, const ImVec2,
                                                         const ImVec2, const ImU32)) {
  if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::Button(id, ImVec2(34.0f, 34.0f));
  if (active) {
    ImGui::PopStyleColor();
  }
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 color =
      ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  drawIcon(*drawList, min, max, color);
  if (ImGui::IsItemHovered() && tooltip && tooltip[0] != '\0') {
    ImGui::SetTooltip("%s", tooltip);
  }
  return clicked;
}

[[nodiscard]] bool drawModeButton(const char* id, const bool active,
                                  const UiOverlay::EditMode mode,
                                  const char* tooltip) {
  if (active) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::Button(id, ImVec2(34.0f, 34.0f));
  if (active) {
    ImGui::PopStyleColor();
  }
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 color =
      ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  drawButtonIcon(*drawList, min, max, mode, color);
  if (ImGui::IsItemHovered() && tooltip && tooltip[0] != '\0') {
    ImGui::SetTooltip("%s", tooltip);
  }
  return clicked;
}

} // namespace

void UiOverlay::attach(CameraRig& rig, LX_core::CommandBus& commandBus,
                       LX_core::EditorState& editorState,
                       EditorConfigDocument& editorConfig,
                       LX_core::SceneTreePanel& sceneTreePanel,
                       LX_core::InspectorPanel& inspectorPanel,
                       LX_core::ConsolePanel& consolePanel,
                       std::function<bool()> debugEnabled) {
  m_rig = std::ref(rig);
  m_commandBus = std::ref(commandBus);
  m_editorState = std::ref(editorState);
  m_editorConfig = std::ref(editorConfig);
  m_sceneTreePanel = std::ref(sceneTreePanel);
  m_inspectorPanel = std::ref(inspectorPanel);
  m_consolePanel = std::ref(consolePanel);
  m_debugEnabled = std::move(debugEnabled);
  syncPanelOpenStatesFromConfig();
  if (!m_baseStyleCaptured && ImGui::GetCurrentContext() != nullptr) {
    m_baseStyle = ImGui::GetStyle();
    m_baseStyleCaptured = true;
  }
  m_toolbarVisible = true;
  applyUiFontScale();
  setEditMode(m_editMode);
}

void UiOverlay::attachClock(const LX_core::Clock& clock) { m_clock = std::cref(clock); }

UiOverlay::EditMode UiOverlay::currentEditMode() const { return m_editMode; }

SceneViewRect UiOverlay::sceneViewRect(const LX_core::Vec2f& windowSize) const {
  if (m_sceneViewRect.isValid()) {
    return m_sceneViewRect;
  }
  return makeSceneViewRect(windowSize.x, windowSize.y, 0.0f, 0.0f, 0.0f, 0.0f);
}

void UiOverlay::setEditMode(const EditMode mode) {
  m_editMode = mode;
  if (!m_rig) {
    return;
  }
  if (mode == EditMode::Orbit) {
    m_rig->get().setMode(CameraRig::Mode::Orbit);
  } else if (mode == EditMode::FreeFly) {
    m_rig->get().setMode(CameraRig::Mode::FreeFly);
  }
}

bool UiOverlay::consumeConfigDirty() {
  const bool dirty = m_configDirty;
  m_configDirty = false;
  return dirty;
}

void UiOverlay::applyUiFontScale() {
  if (!m_editorConfig || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  if (!m_baseStyleCaptured) {
    m_baseStyle = ImGui::GetStyle();
    m_baseStyleCaptured = true;
  }
  const float targetScale =
      clampUiFontScale(m_editorConfig->get().preferences.uiFontScale);
  if (targetScale == m_appliedUiFontScale) {
    return;
  }
  ImGuiStyle style = m_baseStyle;
  style.ScaleAllSizes(targetScale);
  style.FontScaleMain = targetScale;
  ImGui::GetStyle() = style;
  m_appliedUiFontScale = targetScale;
}

void UiOverlay::ensurePanelLayout(std::string_view id,
                                  const PanelDefaults& defaults,
                                  const bool visible) {
  if (!m_editorConfig) {
    return;
  }

  auto existing = findEditorWindowLayout(m_editorConfig->get(), id);
  if (existing.has_value()) {
    return;
  }

  EditorWindowLayout layout;
  layout.id = std::string(id);
  layout.visible = visible;
  layout.collapsed = defaults.collapsed;
  layout.x = static_cast<int>(defaults.x);
  layout.y = static_cast<int>(defaults.y);
  layout.width = static_cast<int>(defaults.width);
  layout.height = static_cast<int>(defaults.height);
  m_editorConfig->get().layoutWindows.push_back(std::move(layout));
  m_configDirty = true;
}

void UiOverlay::syncPanelOpenStatesFromConfig() {
  if (!m_editorConfig) {
    return;
  }
  if (const auto panel = findEditorWindowLayout(m_editorConfig->get(), "Scene Tree");
      panel && m_sceneTreePanel) {
    m_sceneTreePanel->get().setOpen(panel->get().visible);
  }
  if (const auto panel = findEditorWindowLayout(m_editorConfig->get(), "Inspector");
      panel && m_inspectorPanel) {
    m_inspectorPanel->get().setOpen(panel->get().visible);
  }
  if (const auto panel = findEditorWindowLayout(m_editorConfig->get(), "Command Console");
      panel && m_consolePanel) {
    m_consolePanel->get().setOpen(panel->get().visible);
  }
  if (const auto panel = findEditorWindowLayout(m_editorConfig->get(), "Stats");
      panel) {
    m_statsVisible = panel->get().visible;
  }
  if (const auto panel = findEditorWindowLayout(m_editorConfig->get(), "Help");
      panel) {
    m_helpVisible = panel->get().visible;
  }
  if (const auto panel = findEditorWindowLayout(m_editorConfig->get(), "Toolbar");
      panel) {
    m_toolbarVisible = true;
    if (!panel->get().visible) {
      panel->get().visible = true;
      m_configDirty = true;
    }
  }
  if (const auto panel =
          findEditorWindowLayout(m_editorConfig->get(), "Preferences");
      panel) {
    m_preferencesVisible = panel->get().visible;
  }
}

void UiOverlay::ensureInitialPanelLayouts() {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float leftWidth = 280.0f;
  const float rightWidth = 360.0f;
  const float bottomHeight = 220.0f;
  const float topInset = 68.0f;
  const float centerHeight = std::max(1.0f, display.y - bottomHeight);

  ensurePanelLayout("Toolbar", PanelDefaults{12.0f, 12.0f, 252.0f, 94.0f, false},
                    m_toolbarVisible);
  ensurePanelLayout("Stats",
                    PanelDefaults{display.x - rightWidth - 16.0f, 12.0f, rightWidth,
                                  132.0f, false},
                    m_statsVisible);
  ensurePanelLayout("Scene Tree",
                    PanelDefaults{12.0f, topInset, leftWidth,
                                  centerHeight - topInset - 12.0f, false},
                    m_sceneTreePanel ? m_sceneTreePanel->get().isOpen() : true);
  ensurePanelLayout("Inspector",
                    PanelDefaults{display.x - rightWidth - 12.0f, topInset,
                                  rightWidth, centerHeight - topInset - 12.0f, false},
                    m_inspectorPanel ? m_inspectorPanel->get().isOpen() : true);
  ensurePanelLayout("Command Console",
                    PanelDefaults{leftWidth + 24.0f, centerHeight,
                                  std::max(1.0f, display.x - leftWidth - rightWidth - 48.0f),
                                  bottomHeight - 12.0f, false},
                    m_consolePanel ? m_consolePanel->get().isOpen() : true);
  ensurePanelLayout("Help",
                    PanelDefaults{320.0f, 84.0f, 420.0f, 150.0f, false},
                    m_helpVisible);
  ensurePanelLayout("Preferences",
                    PanelDefaults{340.0f, 120.0f, 360.0f, 160.0f, false},
                    m_preferencesVisible);
}

void UiOverlay::applyPanelLayout(std::string_view id,
                                 const PanelDefaults& defaults) {
  if (!m_editorConfig) {
    ImGui::SetNextWindowPos(ImVec2(defaults.x, defaults.y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(defaults.width, defaults.height),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowCollapsed(defaults.collapsed, ImGuiCond_Appearing);
    return;
  }

  const auto layout = findEditorWindowLayout(m_editorConfig->get(), id);
  const ImGuiCond cond = m_initialLayoutApplied ? ImGuiCond_Appearing
                                                : ImGuiCond_Always;
  const EditorWindowLayout resolved = layout.has_value()
                                          ? layout->get()
                                          : EditorWindowLayout{
                                                .id = std::string(id),
                                                .visible = true,
                                                .collapsed = defaults.collapsed,
                                                .x = static_cast<int>(defaults.x),
                                                .y = static_cast<int>(defaults.y),
                                                .width =
                                                    static_cast<int>(defaults.width),
                                                .height =
                                                    static_cast<int>(defaults.height),
                                            };
  ImGui::SetNextWindowPos(ImVec2(static_cast<float>(resolved.x),
                                 static_cast<float>(resolved.y)),
                          cond);
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(std::max(1, resolved.width)),
                                  static_cast<float>(std::max(1, resolved.height))),
                           cond);
  ImGui::SetNextWindowCollapsed(resolved.collapsed, cond);
}

void UiOverlay::syncPanelLayout(std::string_view id, const bool visible) {
  if (!m_editorConfig) {
    return;
  }

  auto layout = findEditorWindowLayout(m_editorConfig->get(), id);
  if (!layout.has_value()) {
    return;
  }
  EditorWindowLayout updated = layout->get();
  updated.visible = visible;

  if (ImGuiWindow* window = ImGui::FindWindowByName(std::string(id).c_str());
      window != nullptr) {
    updated.x = static_cast<int>(window->Pos.x);
    updated.y = static_cast<int>(window->Pos.y);
    updated.width = static_cast<int>(window->Size.x);
    updated.height = static_cast<int>(window->Size.y);
    updated.collapsed = window->Collapsed;
  }

  EditorWindowLayout& target = layout->get();
  if (target.visible != updated.visible || target.collapsed != updated.collapsed ||
      target.x != updated.x || target.y != updated.y ||
      target.width != updated.width || target.height != updated.height) {
    target = updated;
    m_configDirty = true;
  }
}

void UiOverlay::handleHotkeys(LX_core::IInputState& input) {
  const bool previewEnabled =
      m_editorState && m_editorState->get().isPreviewEnabled();
  const bool f1Down = input.isKeyDown(LX_core::KeyCode::F1);
  if (f1Down && !m_prevF1Down) {
    m_helpVisible = !m_helpVisible;
    m_configDirty = true;
  }
  m_prevF1Down = f1Down;

  const bool fDown = input.isKeyDown(LX_core::KeyCode::F);
  if (fDown && !m_prevFDown && m_commandBus) {
    (void)m_commandBus->get().dispatch("preview toggle");
  }
  m_prevFDown = fDown;

  const bool escapeDown = input.isKeyDown(LX_core::KeyCode::Escape);
  if (!previewEnabled && escapeDown && !m_prevEscapeDown && m_commandBus &&
      m_editMode == EditMode::Selection) {
    (void)m_commandBus->get().dispatch("deselect");
  }
  m_prevEscapeDown = escapeDown;

  const bool deleteDown = input.isKeyDown(LX_core::KeyCode::Delete);
  if (!previewEnabled && deleteDown && !m_prevDeleteDown && m_commandBus &&
      m_editorState) {
    const auto primarySelected = m_editorState->get().getPrimarySelected();
    if (primarySelected.has_value()) {
      (void)m_commandBus->get().dispatch(
          "remove " + quoteToken(primarySelected->get().getPath()));
    }
  }
  m_prevDeleteDown = deleteDown;
}

void UiOverlay::drawToolbarPanel() {
  applyPanelLayout("Toolbar", PanelDefaults{12.0f, 12.0f, 252.0f, 94.0f, false});
  if (!ImGui::Begin("Toolbar", nullptr,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    syncPanelLayout("Toolbar", true);
    return;
  }

  const bool previewEnabled =
      m_editorState && m_editorState->get().isPreviewEnabled();
  const bool editingEnabled = !previewEnabled;

  if (!editingEnabled) {
    ImGui::BeginDisabled();
  }
  if (drawModeButton("##tool_select", m_editMode == EditMode::Selection,
                     EditMode::Selection, "Selection")) {
    setEditMode(EditMode::Selection);
  }
  ImGui::SameLine();
  if (drawModeButton("##tool_orbit", m_editMode == EditMode::Orbit,
                     EditMode::Orbit, "Orbit")) {
    setEditMode(EditMode::Orbit);
  }
  ImGui::SameLine();
  if (drawModeButton("##tool_freefly", m_editMode == EditMode::FreeFly,
                     EditMode::FreeFly, "FreeFly")) {
    setEditMode(EditMode::FreeFly);
  }
  if (!editingEnabled) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (drawIconToggleButton("##tool_reset_editor_camera", false,
                           "Reset editor camera from game camera",
                           drawResetIcon) &&
      m_commandBus) {
    (void)m_commandBus->get().dispatch("cam reset-editor-to-game");
  }

  ImGui::SameLine();
  if (drawIconToggleButton("##tool_preview", previewEnabled, "Preview",
                           drawPreviewIcon) &&
      m_commandBus) {
    (void)m_commandBus->get().dispatch("preview toggle");
  }

  ImGui::SameLine();
  if (drawIconToggleButton("##tool_preferences", m_preferencesVisible,
                           "Preferences", drawPreferencesIcon)) {
    m_preferencesVisible = !m_preferencesVisible;
    m_configDirty = true;
  }

  ImGui::Separator();
  const bool debugEnabled = m_debugEnabled && m_debugEnabled();
  if (drawIconToggleButton("##tool_debug", debugEnabled, "Debug",
                           drawPreferencesIcon) &&
      m_commandBus) {
    (void)m_commandBus->get().dispatch(debugEnabled ? "debug off" : "debug on");
  }

  ImGui::End();
  syncPanelLayout("Toolbar", true);
}

void UiOverlay::drawStatsPanel() {
  if (!m_statsVisible) {
    syncPanelLayout("Stats", false);
    return;
  }

  const ImVec2 display = ImGui::GetIO().DisplaySize;
  applyPanelLayout("Stats",
                   PanelDefaults{display.x - 372.0f, 12.0f, 360.0f, 132.0f, false});
  if (!ImGui::Begin("Stats", &m_statsVisible)) {
    ImGui::End();
    syncPanelLayout("Stats", m_statsVisible);
    return;
  }
  if (m_clock) {
    dui::renderStatsPanel(m_clock->get());
  }
  if (m_editorState) {
    dui::labelText("preview",
                   m_editorState->get().isPreviewEnabled() ? "Game" : "Editor");
  }
  dui::labelText("edit mode", editModeLabel(m_editMode));
  ImGui::End();
  syncPanelLayout("Stats", m_statsVisible);
}

void UiOverlay::drawHelpPanel() {
  if (!m_helpVisible) {
    syncPanelLayout("Help", false);
    return;
  }

  applyPanelLayout("Help", PanelDefaults{320.0f, 84.0f, 420.0f, 150.0f, false});
  if (!ImGui::Begin("Help", &m_helpVisible)) {
    ImGui::End();
    syncPanelLayout("Help", m_helpVisible);
    return;
  }
  ImGui::TextUnformatted("F1  toggle this help panel");
  ImGui::TextUnformatted("F   preview toggle");
  ImGui::TextUnformatted("Toolbar  Selection / Orbit / FreeFly / Preview / Preferences");
  ImGui::TextUnformatted("Toolbar row 2  Debug toggle");
  ImGui::TextUnformatted("Selection mode captures scene clicks outside UI");
  ImGui::TextUnformatted("Esc deselect in Selection mode | Delete remove when preview is off");
  ImGui::End();
  syncPanelLayout("Help", m_helpVisible);
}

void UiOverlay::drawPreferencesPanel() {
  if (!m_preferencesVisible || !m_editorConfig) {
    syncPanelLayout("Preferences", m_preferencesVisible);
    return;
  }

  applyPanelLayout("Preferences",
                   PanelDefaults{340.0f, 120.0f, 360.0f, 160.0f, false});
  if (!ImGui::Begin("Preferences", &m_preferencesVisible)) {
    ImGui::End();
    syncPanelLayout("Preferences", m_preferencesVisible);
    return;
  }

  ImGui::SeparatorText("Appearance");
  float fontScale = clampUiFontScale(m_editorConfig->get().preferences.uiFontScale);
  if (ImGui::SliderFloat("UI Font Scale", &fontScale, kMinUiFontScale,
                         kMaxUiFontScale, "%.2f")) {
    m_editorConfig->get().preferences.uiFontScale = clampUiFontScale(fontScale);
    m_configDirty = true;
    applyUiFontScale();
  }

  ImGui::End();
  syncPanelLayout("Preferences", m_preferencesVisible);
}

void UiOverlay::drawFrame() {
  if (!m_editorConfig || ImGui::GetCurrentContext() == nullptr) {
    return;
  }

  ensureInitialPanelLayouts();
  applyUiFontScale();

  drawToolbarPanel();
  drawStatsPanel();

  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float leftWidth = 280.0f;
  const float rightWidth = 360.0f;
  const float bottomHeight = 220.0f;
  const float topInset = 68.0f;
  const float centerHeight = std::max(1.0f, display.y - bottomHeight);

  if (m_sceneTreePanel) {
    applyPanelLayout("Scene Tree",
                     PanelDefaults{12.0f, topInset, leftWidth,
                                   centerHeight - topInset - 12.0f, false});
    m_sceneTreePanel->get().draw();
    syncPanelLayout("Scene Tree", m_sceneTreePanel->get().isOpen());
  }

  if (m_inspectorPanel) {
    applyPanelLayout("Inspector",
                     PanelDefaults{display.x - rightWidth - 12.0f, topInset,
                                   rightWidth, centerHeight - topInset - 12.0f, false});
    m_inspectorPanel->get().draw();
    syncPanelLayout("Inspector", m_inspectorPanel->get().isOpen());
  }

  if (m_consolePanel) {
    applyPanelLayout("Command Console",
                     PanelDefaults{leftWidth + 24.0f, centerHeight,
                                   std::max(1.0f, display.x - leftWidth - rightWidth - 48.0f),
                                   bottomHeight - 12.0f, false});
    m_consolePanel->get().draw();
    syncPanelLayout("Command Console", m_consolePanel->get().isOpen());
  }

  drawHelpPanel();
  drawPreferencesPanel();
  m_sceneViewRect =
      makeSceneViewRect(display.x, display.y,
                        maxWindowRight("Scene Tree", 12.0f) + 12.0f,
                        maxWindowBottom("Toolbar", 12.0f),
                        rightInsetFromWindow("Inspector", display.x, 12.0f) +
                            12.0f,
                        bottomInsetFromWindowTop("Command Console", display.y,
                                                 12.0f));
  m_initialLayoutApplied = true;
}

} // namespace LX_demo::lxe_editor
