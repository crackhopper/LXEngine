#include "core/editor/scene_tree_panel.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace LX_core {
namespace {

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

[[nodiscard]] bool hasLiveChildren(const SceneNode &node) {
  return !node.getChildren().empty();
}

[[nodiscard]] bool isAncestorOrSelf(std::string_view ancestor,
                                    std::string_view candidate) {
  if (ancestor.empty() || candidate.empty()) {
    return false;
  }
  if (ancestor == candidate) {
    return true;
  }
  if (ancestor.size() >= candidate.size()) {
    return false;
  }
  if (candidate.compare(0, ancestor.size(), ancestor) != 0) {
    return false;
  }
  return candidate[ancestor.size()] == '/';
}

} // namespace

SceneTreePanel::SceneTreePanel(CommandBus &commandBus, EditorState &editorState,
                               Scene &scene)
    : m_commandBus(commandBus), m_editorState(editorState), m_scene(scene) {}

void SceneTreePanel::draw() {
  if (!m_open) {
    return;
  }

  if (!ImGui::Begin("Scene Tree", &m_open)) {
    ImGui::End();
    return;
  }

  const bool submitted = ImGui::InputText(
      "Path##scene_tree_jump", m_pathInputBuffer.data(), m_pathInputBuffer.size(),
      ImGuiInputTextFlags_EnterReturnsTrue);
  if (submitted) {
    (void)submitPathJump();
  }

  ImGui::Separator();

  for (const auto &root : m_scene.getRootNodes()) {
    if (!root) {
      continue;
    }
    drawNode(*root);
  }

  ImGui::End();
}

void SceneTreePanel::setPathInputText(std::string_view text) {
  const usize copyLength = std::min(text.size(), m_pathInputBuffer.size() - 1);
  std::fill(m_pathInputBuffer.begin(), m_pathInputBuffer.end(), '\0');
  if (copyLength > 0) {
    std::memcpy(m_pathInputBuffer.data(), text.data(), copyLength);
  }
}

std::string SceneTreePanel::getPathInputText() const {
  return std::string(m_pathInputBuffer.data());
}

CommandResult SceneTreePanel::submitPathJump() {
  const std::string path = trim(getPathInputText());
  if (path.empty()) {
    return CommandResult{false, "empty path", {}};
  }

  const CommandResult result = dispatchSelectPath(path);
  if (result.ok) {
    setPathInputText(path);
    m_revealPath = path;
  }
  return result;
}

CommandResult SceneTreePanel::dispatchSelectPath(std::string_view path) {
  return m_commandBus.dispatch("select " + std::string(path));
}

CommandResult SceneTreePanel::dispatchRemovePath(std::string_view path) {
  return m_commandBus.dispatch("remove " + std::string(path));
}

void SceneTreePanel::drawNode(SceneNode &node) {
  if (!m_revealPath.empty() && isAncestorOrSelf(node.getPath(), m_revealPath)) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  }

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_SpanAvailWidth;
  if (!hasLiveChildren(node)) {
    flags |= ImGuiTreeNodeFlags_Leaf;
  }
  if (const auto selected = m_editorState.getSelected();
      selected && selected.get() == &node) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }

  const std::string label = node.getName().empty() ? node.getNodeName() : node.getName();
  const bool open = ImGui::TreeNodeEx(static_cast<void *>(&node), flags, "%s", label.c_str());

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    (void)dispatchSelectPath(node.getPath());
  }

  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::MenuItem("Remove")) {
      (void)dispatchRemovePath(node.getPath());
    }
    ImGui::EndPopup();
  }

  if (open) {
    for (const auto &child : node.getChildren()) {
      if (child) {
        drawNode(*child);
      }
    }
    ImGui::TreePop();
  }

  if (!m_revealPath.empty() && node.getPath() == m_revealPath) {
    m_revealPath.clear();
  }
}

} // namespace LX_core
