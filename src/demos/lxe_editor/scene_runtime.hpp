#pragma once

#include "core/asset/material_instance.hpp"
#include "core/editor/command_bus.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/scene_document.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LX_demo::lxe_editor {

struct RuntimeMaterialParameterValue final {
  std::string binding;
  std::string member;
  LX_core::MaterialParameterValue value;
  bool runtimeOwned = false;
};

class SceneRuntime final {
public:
  SceneRuntime() = default;
  SceneRuntime(const SceneRuntime&) = delete;
  SceneRuntime(SceneRuntime&&) noexcept = default;
  SceneRuntime& operator=(const SceneRuntime&) = delete;
  SceneRuntime& operator=(SceneRuntime&&) noexcept = default;
  ~SceneRuntime() = default;

  void createEmptyScene();
  void loadFromDocumentPath(const std::filesystem::path& path);
  void saveToCurrentDocumentPath();
  void saveToDocumentPath(const std::filesystem::path& path);
  [[nodiscard]] std::optional<std::filesystem::path> documentPath() const;
  [[nodiscard]] const SceneDocument &document() const;
  [[nodiscard]] LX_core::SceneSharedPtr scene() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr editorCameraNode() const;
  [[nodiscard]] LX_core::SceneNodeSharedPtr gameCameraNode() const;
  [[nodiscard]] std::optional<std::string>
  materialUriForNode(const std::string& path) const;
  [[nodiscard]] std::optional<LX_core::Vec3f>
  nodeMaterialBaseColorForNode(const std::string& path) const;
  [[nodiscard]] bool nodeMaterialBaseColorEditable(const std::string& path) const;
  [[nodiscard]] std::vector<std::string> materialPresets() const;
  [[nodiscard]] std::optional<LX_core::MaterialParameterValue>
  nodeMaterialParameterForNode(const std::string& path,
                               const std::string& binding,
                               const std::string& member) const;
  [[nodiscard]] std::vector<RuntimeMaterialParameterValue>
  nodeMaterialParametersForNode(const std::string& path) const;
  [[nodiscard]] std::optional<bool>
  proceduralMaterialEnabledForNode(const std::string& path) const;
  std::vector<std::string>
  updateProceduralMaterials(float totalTime,
                            const LX_core::Vec2f& resolution);
  LX_core::CommandResult setNodeMaterialUri(const std::string& path,
                                            const std::string& uri);
  LX_core::CommandResult setNodeMaterialBaseColor(
      const std::string& path, const LX_core::Vec3f& color);
  LX_core::CommandResult setNodeMaterialParameter(
      const std::string& path, const std::string& binding,
      const std::string& member,
      const LX_core::MaterialParameterValue& value);
  LX_core::CommandResult clearNodeMaterialParameter(
      const std::string& path, const std::string& binding,
      const std::string& member);
  LX_core::CommandResult
  setNodeProceduralMaterialEnabled(const std::string& path, bool enabled);
  LX_core::CommandResult applyMaterialOverride(const std::string& path,
                                               const std::string& field);

private:
  std::shared_ptr<void> m_impl;
};

} // namespace LX_demo::lxe_editor
