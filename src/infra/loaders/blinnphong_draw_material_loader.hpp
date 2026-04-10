#pragma once

#include "core/gpu/render_resource.hpp"
#include "core/resources/material.hpp"

namespace LX_infra {

/// Compile `blinnphong_0` GLSL from `shaders/glsl/` (current working directory must
/// already be set, e.g. via `cdToWhereShadersExist("blinnphong_0")`) and build a
/// `DrawMaterial` with reflection-backed `IShader`.
LX_core::DrawMaterial::Ptr
loadBlinnPhongDrawMaterial(LX_core::ResourcePassFlag passFlag =
                               LX_core::ResourcePassFlag::Forward);

} // namespace LX_infra
