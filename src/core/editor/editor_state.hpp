#pragma once

#include "core/scene/object.hpp"

namespace LX_core {

class Scene;


class EditorState final {
public:
  void select(const SceneNodeSharedPtr &node);
  void deselect();
  [[nodiscard]] SceneNodeSharedPtr getSelected() const;

  void setPreviewEnabled(bool enabled);
  void togglePreviewEnabled();
  [[nodiscard]] bool isPreviewEnabled() const;

  void setEditorCamera(const SceneNodeSharedPtr &node);
  void setPreviewCamera(const SceneNodeSharedPtr &node);
  [[nodiscard]] SceneNodeSharedPtr getEditorCamera() const;
  [[nodiscard]] SceneNodeSharedPtr getPreviewCamera() const;
  [[nodiscard]] SceneNodeSharedPtr resolveActiveCamera(const Scene &scene) const;
  [[nodiscard]] SceneNodeSharedPtr syncActiveCamera(Scene &scene) const;

private:
  std::weak_ptr<SceneNode> m_selected;
  std::weak_ptr<SceneNode> m_editorCamera;
  std::weak_ptr<SceneNode> m_previewCamera;
  bool m_previewEnabled = false;
};

} // namespace LX_core
