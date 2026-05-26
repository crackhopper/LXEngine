#pragma once

// Demo-local glue that bridges GLTFLoader output and editor/runtime scene
// nodes. Intentionally not lowered into src/infra/ because it still owns
// lxe_editor-specific built-in asset choices.

#include "core/asset/material_instance.hpp"
#include "core/scene/object.hpp"
#include "core/scene/visibility_mask.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {

inline constexpr LX_core::VisibilityLayerMask Layer_EditorHelper = 1u << 30;

// Loads DamagedHelmet.gltf, bridges its PBR texture metadata into the PBR
// material contract, and returns a SceneNode ready to attach to a Scene.
// Throws std::runtime_error on failure.
LX_core::SceneNodeSharedPtr
buildHelmetNode(const std::filesystem::path &gltfPath);

// Builds a 20m x 20m XZ ground plane (y = 0) with the Blinn-Phong material,
// albedo sampling disabled. Returns a SceneNode ready to attach.
LX_core::SceneNodeSharedPtr buildGroundNode();

LX_core::SceneNodeSharedPtr buildBuiltinPrimitiveNode(std::string_view meshUri,
                                                      std::string nodeName);

// Builds a non-closed surface patch. Patch nodes render in Forward but do not
// cast shadows by default, because a single surface has no stable front/back
// volume for Shadow pass self-occlusion.
LX_core::SceneNodeSharedPtr buildBuiltinPatchNode(std::string_view meshUri,
                                                  std::string nodeName);

LX_core::SceneNodeSharedPtr
buildModelAssetNode(std::string_view meshUri, std::string_view materialUri,
                    std::string_view albedoTextureUri, std::string nodeName);

void bindModelAlbedoTexture(LX_core::MaterialInstanceSharedPtr material,
                            std::string_view albedoTextureUri);

} // namespace LX_demo::lxe_editor
