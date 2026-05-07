#include "core/editor/editor_state.hpp"

#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"

namespace LX_core {

void EditorState::select(const SceneNodeSharedPtr &node) { m_selected = node; }

void EditorState::deselect() { m_selected.reset(); }

SceneNodeSharedPtr EditorState::getSelected() const { return m_selected.lock(); }

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
