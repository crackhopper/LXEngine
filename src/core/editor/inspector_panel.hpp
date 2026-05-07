#pragma once

#include "core/editor/command_bus.hpp"
#include "core/math/vec.hpp"

#include <array>
#include <string>
#include <string_view>

namespace LX_core {

class EditorState;
class SceneNode;

class InspectorPanel final {
public:
  struct Snapshot {
    bool hasSelection = false;
    std::string path;
    std::string name;
    Vec3f translation{0.0f, 0.0f, 0.0f};
    Vec3f rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    Vec3f scale{1.0f, 1.0f, 1.0f};
    bool hasCamera = false;
    bool hasMesh = false;
    bool hasMaterial = false;
    bool hasSkeleton = false;
  };

  InspectorPanel(CommandBus &commandBus, EditorState &editorState);

  void draw();

  [[nodiscard]] Snapshot makeSnapshot() const;
  [[nodiscard]] CommandResult dispatchRename(std::string_view path,
                                             std::string_view newName);
  [[nodiscard]] CommandResult dispatchMove(std::string_view path,
                                           const Vec3f &translation);
  [[nodiscard]] CommandResult dispatchRotate(std::string_view path,
                                             const Vec3f &rotationEulerDegrees);
  [[nodiscard]] CommandResult dispatchScale(std::string_view path,
                                            const Vec3f &scale);

private:
  void syncDraftFromSnapshot(const Snapshot &snapshot);
  void drawSelection(const Snapshot &snapshot);

  CommandBus &m_commandBus;
  EditorState &m_editorState;
  std::string m_syncedSelectionPath;
  std::array<char, 256> m_nameBuffer{};
  Vec3f m_translationDraft{0.0f, 0.0f, 0.0f};
  Vec3f m_rotationDraft{0.0f, 0.0f, 0.0f};
  Vec3f m_scaleDraft{1.0f, 1.0f, 1.0f};
  bool m_open = true;
};

} // namespace LX_core
