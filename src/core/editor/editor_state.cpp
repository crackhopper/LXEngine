#include "core/editor/editor_state.hpp"

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
