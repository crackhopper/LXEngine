#pragma once
#include "core/asset/material.hpp"

namespace LX_infra {

/// Compile `blinnphong_0.{vert,frag}`, reflect bindings, build a single-pass
/// `MaterialTemplate`, and return a seeded `MaterialInstance::Ptr`.
/// Replaces the legacy `loadBlinnPhongDrawMaterial` entry point.
LX_core::MaterialInstance::Ptr loadBlinnPhongMaterial(
    LX_core::ResourcePassFlag passFlag = LX_core::ResourcePassFlag::Forward,
    std::vector<LX_core::ShaderVariant> variants = {
        LX_core::ShaderVariant{"USE_VERTEX_COLOR", false},
        LX_core::ShaderVariant{"USE_UV", false},
        LX_core::ShaderVariant{"USE_LIGHTING", true},
        LX_core::ShaderVariant{"USE_NORMAL_MAP", false},
        LX_core::ShaderVariant{"USE_SKINNING", false},
    });

} // namespace LX_infra
