#pragma once

// REQ-019: demo-local glue that bridges GLTFLoader output and the current
// Blinn-Phong material system. Intentionally not lowered into src/infra/;
// this is transitional until a full PBR material loader lands.

#include "core/scene/object.hpp"

#include <filesystem>

namespace LX_demo::scene_viewer {

// Loads DamagedHelmet.gltf, bridges its PBR texture metadata into the
// existing blinnphong_0 material, and returns a SceneNode ready to attach to
// a Scene. Throws std::runtime_error on failure.
LX_core::SceneNodeSharedPtr buildHelmetNode(const std::filesystem::path& gltfPath);

// Builds a 20m x 20m XZ ground plane (y = 0) with the Blinn-Phong material,
// albedo sampling disabled. Returns a SceneNode ready to attach.
LX_core::SceneNodeSharedPtr buildGroundNode();

} // namespace LX_demo::scene_viewer
