#pragma once

#include "core/offline/offline_scene.hpp"
#include "infra/offline/offline_asset_resolver.hpp"
#include "infra/scene_io/scene_document.hpp"

#include <filesystem>
#include <string>

namespace LX_infra::offline {

/*
@source_analysis.section Compiler 把 editor 文档翻译成离线输入
`OfflineSceneCompiler` 位于 `infra`，因为它同时理解 scene YAML 文档、
资产 URI 解析和 core 层 `OfflineSceneIR`。它的职责不是渲染，也不是保存
editor 状态，而是把可见 mesh instance、材质参数、相机、方向光和环境配置
整理成离线 renderer 可消费的紧凑数据。

这个边界让 `lxe_offline_render` CLI 不依赖 `src/demos/lxe_editor/`。
后续支持 glTF、HDR environment、albedo texture 或 bake cache 时，优先扩展
compiler/resolver 到 IR 的这条输入链路，而不是让 Vulkan offline renderer
反向读取 editor 数据结构。
*/
class OfflineSceneCompiler final {
public:
  explicit OfflineSceneCompiler(OfflineAssetResolver resolver);

  [[nodiscard]] LX_core::offline::OfflineSceneIR compile(
      const LX_infra::scene_io::SceneDocument &document,
      const std::string &cameraPath) const;

  [[nodiscard]] LX_core::offline::OfflineSceneIR compileFile(
      const std::filesystem::path &path, const std::string &cameraPath) const;

private:
  OfflineAssetResolver m_resolver;
};

} // namespace LX_infra::offline
