#pragma once

#include "core/editor/command_bus.hpp"

#include <array>
#include <string>
#include <string_view>

namespace LX_core {

class EditorState;
class Scene;
class SceneNode;

class SceneTreePanel final {
public:
  SceneTreePanel(CommandBus &commandBus, EditorState &editorState, Scene &scene);

  void draw();

  void setPathInputText(std::string_view text);
  [[nodiscard]] std::string getPathInputText() const;

  [[nodiscard]] CommandResult submitPathJump();
  [[nodiscard]] CommandResult dispatchSelectPath(std::string_view path);
  [[nodiscard]] CommandResult dispatchRemovePath(std::string_view path);

private:
  void drawNode(SceneNode &node);

  CommandBus &m_commandBus;
  EditorState &m_editorState;
  Scene &m_scene;
  std::array<char, 512> m_pathInputBuffer{};
  bool m_open = true;
};

} // namespace LX_core
