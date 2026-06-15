#include "editor/render/editor_render_view.hpp"

#include "editor/app/editor_state.hpp"

#include "core/scene/components/camera_component.hpp"

namespace LX_editor {

std::optional<EditorRenderView>
buildEditorRenderView(const LX_core::EditorState &editorState,
                      const LX_core::Scene &scene,
                      const LX_core::Vec2f &viewportExtent) {
  const LX_core::SceneNodeSharedPtr cameraNode =
      editorState.resolveActiveCamera(scene);
  if (!cameraNode) {
    return std::nullopt;
  }

  const auto cameraComponent = cameraNode->getComponent<LX_core::CameraComponent>();
  if (!cameraComponent.has_value()) {
    return std::nullopt;
  }

  const std::string cameraPath = cameraNode->getPath();
  LX_core::CameraResource cameraResource =
      LX_core::Scene::makeCameraResource(
          cameraComponent->get().getSnapshot(cameraPath));
  cameraResource.active = true;
  cameraResource.cullingMask = cameraComponent->get().getCullingMask();

  return EditorRenderView{
      .cameraPath = cameraPath,
      .cameraResource = cameraResource,
      .visibleMask = cameraComponent->get().getCullingMask() &
                     ~LX_core::Layer_EditorOverlay,
      .viewportExtent = viewportExtent,
      .previewEnabled = editorState.isPreviewEnabled(),
      .editorOverlayVisible = !editorState.isPreviewEnabled(),
  };
}

} // namespace LX_editor
