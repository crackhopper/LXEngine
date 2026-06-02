#pragma once

#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_scene.hpp"

#include <filesystem>

namespace LX_infra::image {

void writeRgba32fExr(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image);
void writeToneMappedPng(const std::filesystem::path &path,
                        const LX_core::offline::OfflineReadbackImage &image,
                        const LX_core::image::ToneMappingSettings &settings);
void writeRawRgba32f(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image);

} // namespace LX_infra::image
