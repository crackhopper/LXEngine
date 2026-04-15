#pragma once
#include "core/asset/material.hpp"

namespace LX_infra {

/// Compile `blinnphong_0.{vert,frag}`, reflect bindings, build a single-pass
/// `MaterialTemplate`, and return a seeded `MaterialInstance::Ptr`.
/// Replaces the legacy `loadBlinnPhongDrawMaterial` entry point.
LX_core::MaterialInstance::Ptr loadBlinnPhongMaterial(
    LX_core::ResourcePassFlag passFlag = LX_core::ResourcePassFlag::Forward);

} // namespace LX_infra
