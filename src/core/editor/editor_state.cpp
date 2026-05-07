#include "core/editor/editor_state.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {

bool EditorState::containsNode(
    const std::vector<std::weak_ptr<SceneNode>> &selection,
    const SceneNodeSharedPtr &node) {
  if (!node) {
    return false;
  }

  for (const auto &entry : selection) {
    if (const auto locked = entry.lock(); locked && locked.get() == node.get()) {
      return true;
    }
  }
  return false;
}

void EditorState::select(std::vector<SceneNodeSharedPtr> nodes) {
  m_selected.clear();
  for (const auto &node : nodes) {
    if (!node || containsNode(m_selected, node)) {
      continue;
    }
    m_selected.emplace_back(node);
  }
}

void EditorState::selectAdd(const SceneNodeSharedPtr &node) {
  if (!node || containsNode(m_selected, node)) {
    return;
  }
  m_selected.emplace_back(node);
}

void EditorState::selectRemove(const SceneNodeSharedPtr &node) {
  if (!node) {
    return;
  }

  std::vector<std::weak_ptr<SceneNode>> retained;
  retained.reserve(m_selected.size());
  for (const auto &entry : m_selected) {
    const auto locked = entry.lock();
    if (locked && locked.get() != node.get()) {
      retained.emplace_back(locked);
    }
  }
  m_selected = std::move(retained);
}

void EditorState::deselect() { m_selected.clear(); }

std::vector<SceneNodeSharedPtr> EditorState::getSelected() const {
  std::vector<SceneNodeSharedPtr> selected;
  selected.reserve(m_selected.size());
  for (const auto &entry : m_selected) {
    if (const auto locked = entry.lock()) {
      selected.push_back(locked);
    }
  }
  return selected;
}

std::optional<std::reference_wrapper<SceneNode>>
EditorState::getPrimarySelected() const {
  for (auto it = m_selected.rbegin(); it != m_selected.rend(); ++it) {
    if (const auto locked = it->lock()) {
      return std::ref(*locked);
    }
  }
  return std::nullopt;
}

void EditorState::setPreviewEnabled(const bool enabled) {
  m_previewEnabled = enabled;
}

void EditorState::togglePreviewEnabled() { m_previewEnabled = !m_previewEnabled; }

bool EditorState::isPreviewEnabled() const { return m_previewEnabled; }

} // namespace LX_core

void LX_core::EditorState::setEditorCamera(const SceneNodeSharedPtr &node) { m_editorCamera = node; }

void LX_core::EditorState::setPreviewCamera(const SceneNodeSharedPtr &node) { m_previewCamera = node; }

LX_core::SceneNodeSharedPtr LX_core::EditorState::getEditorCamera() const { return m_editorCamera.lock(); }

LX_core::SceneNodeSharedPtr LX_core::EditorState::getPreviewCamera() const { return m_previewCamera.lock(); }

LX_core::SceneNodeSharedPtr LX_core::EditorState::resolveActiveCamera(const Scene &scene) const {
  SceneNodeSharedPtr preferred = isPreviewEnabled() ? getPreviewCamera() : getEditorCamera();
  if (preferred && preferred->getComponent<CameraComponent>().has_value()) {
    return preferred;
  }

  const SceneNodeSharedPtr fallbackEditor = getEditorCamera();
  if (fallbackEditor && fallbackEditor->getComponent<CameraComponent>().has_value()) {
    return fallbackEditor;
  }

  const SceneNodeSharedPtr fallbackPreview = getPreviewCamera();
  if (fallbackPreview && fallbackPreview->getComponent<CameraComponent>().has_value()) {
    return fallbackPreview;
  }

  for (const auto &cameraNode : scene.getCameras()) {
    if (cameraNode && cameraNode->getComponent<CameraComponent>().has_value()) {
      return cameraNode;
    }
  }
  return {};
}

LX_core::SceneNodeSharedPtr LX_core::EditorState::syncActiveCamera(Scene &scene) const {
  const SceneNodeSharedPtr activeNode = resolveActiveCamera(scene);
  for (const auto &cameraNode : scene.getCameras()) {
    if (!cameraNode) {
      continue;
    }
    auto camera = cameraNode->getComponent<CameraComponent>();
    if (!camera.has_value()) {
      continue;
    }
    camera->get().setActive(activeNode && cameraNode.get() == activeNode.get());
  }
  return activeNode;
}
