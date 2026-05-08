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

void appendUniquePath(std::vector<std::string> &paths, std::string_view path) {
  const auto it = std::find(paths.begin(), paths.end(), path);
  if (it != paths.end()) {
    paths.erase(it);
  }
  paths.emplace_back(path);
}

[[nodiscard]] bool shareSelectionParent(const SceneNode &lhs,
                                        const SceneNode &rhs) {
  const auto lhsParent = lhs.getParent();
  const auto rhsParent = rhs.getParent();
  if (!lhsParent || !rhsParent) {
    return !lhsParent && !rhsParent;
  }
  return lhsParent.get() == rhsParent.get();
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

CommandResult SceneTreePanel::handleNodeClick(SceneNode &node, const bool ctrlHeld,
                                              const bool shiftHeld) {
  if (shiftHeld) {
    return dispatchSelectionPaths(buildSiblingRangeSelectionPaths(node));
  }
  if (ctrlHeld) {
    return dispatchSelectionPaths(buildAdditiveSelectionPaths(node));
  }
  return dispatchSelectPath(node.getPath());
}

CommandResult
SceneTreePanel::dispatchSelectionPaths(const std::vector<std::string> &paths) {
  if (paths.empty()) {
    return m_commandBus.dispatch("deselect");
  }

  std::string command = "select";
  for (const auto &path : paths) {
    command += " " + path;
  }
  return m_commandBus.dispatch(command);
}

std::vector<std::string>
SceneTreePanel::buildAdditiveSelectionPaths(const SceneNode &node) const {
  std::vector<std::string> paths;
  for (const auto &selected : m_editorState.getSelected()) {
    if (selected) {
      appendUniquePath(paths, selected->getPath());
    }
  }
  appendUniquePath(paths, node.getPath());
  return paths;
}

std::vector<std::string>
SceneTreePanel::buildSiblingRangeSelectionPaths(const SceneNode &node) const {
  const auto primarySelected = m_editorState.getPrimarySelected();
  if (!primarySelected.has_value()) {
    return {node.getPath()};
  }

  SceneNode &anchor = primarySelected->get();
  if (!shareSelectionParent(anchor, node)) {
    return {node.getPath()};
  }

  const auto parent = node.getParent();
  const std::vector<SceneNodeSharedPtr> siblings =
      parent ? parent->getChildren() : m_scene.getRootNodes();

  usize anchorIndex = siblings.size();
  usize targetIndex = siblings.size();
  for (usize i = 0; i < siblings.size(); ++i) {
    if (!siblings[i]) {
      continue;
    }
    if (siblings[i].get() == &anchor) {
      anchorIndex = i;
    }
    if (siblings[i].get() == &node) {
      targetIndex = i;
    }
  }

  if (anchorIndex >= siblings.size() || targetIndex >= siblings.size()) {
    return {node.getPath()};
  }

  std::vector<std::string> paths;
  for (const auto &selected : m_editorState.getSelected()) {
    if (selected) {
      appendUniquePath(paths, selected->getPath());
    }
  }

  const usize begin = std::min(anchorIndex, targetIndex);
  const usize end = std::max(anchorIndex, targetIndex);
  for (usize i = begin; i <= end; ++i) {
    if (siblings[i]) {
      appendUniquePath(paths, siblings[i]->getPath());
    }
  }
  return paths;
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
  for (const auto &selected : m_editorState.getSelected()) {
    if (selected && selected.get() == &node) {
      flags |= ImGuiTreeNodeFlags_Selected;
      break;
    }
  }

  const std::string label = node.getName().empty() ? node.getNodeName() : node.getName();
  const bool open = ImGui::TreeNodeEx(static_cast<void *>(&node), flags, "%s", label.c_str());

  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    const ImGuiIO &io = ImGui::GetIO();
    (void)handleNodeClick(node, io.KeyCtrl, io.KeyShift);
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
