#pragma once

#include "core/scene/object.hpp"

namespace LX_core {

class EditorState final {
public:
  void select(const SceneNodeSharedPtr &node);
  void deselect();
  [[nodiscard]] SceneNodeSharedPtr getSelected() const;

  void setPreviewEnabled(bool enabled);
  void togglePreviewEnabled();
  [[nodiscard]] bool isPreviewEnabled() const;

private:
  std::weak_ptr<SceneNode> m_selected;
  bool m_previewEnabled = false;
};

} // namespace LX_core
