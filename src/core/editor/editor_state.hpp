#pragma once

#include "core/scene/object.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace LX_core {

class Scene;

class EditorState final {
public:
  void select(std::vector<SceneNodeSharedPtr> nodes);
  void selectAdd(const SceneNodeSharedPtr &node);
  void selectRemove(const SceneNodeSharedPtr &node);
  void deselect();
  [[nodiscard]] std::vector<SceneNodeSharedPtr> getSelected() const;
  [[nodiscard]] std::optional<std::reference_wrapper<SceneNode>>
  getPrimarySelected() const;

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
  [[nodiscard]] static bool containsNode(
      const std::vector<std::weak_ptr<SceneNode>> &selection,
      const SceneNodeSharedPtr &node);

  std::vector<std::weak_ptr<SceneNode>> m_selected;
  std::weak_ptr<SceneNode> m_editorCamera;
  std::weak_ptr<SceneNode> m_previewCamera;
  bool m_previewEnabled = false;
};

} // namespace LX_core
