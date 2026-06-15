#pragma once

#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_render_job.hpp"

#include <filesystem>
#include <vector>

namespace LX_infra::image {

void writeRgba32fExr(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image);
[[nodiscard]] LX_core::offline::OfflineReadbackImage
readRgba32fExr(const std::filesystem::path &path);
void writeToneMappedPng(const std::filesystem::path &path,
                        const LX_core::offline::OfflineReadbackImage &image,
                        const LX_core::image::ToneMappingSettings &settings);
void writeRawRgba8Png(const std::filesystem::path &path, u32 width, u32 height,
                      const std::vector<unsigned char> &rgba);
void writeRawRgba32f(const std::filesystem::path &path,
                     const LX_core::offline::OfflineReadbackImage &image);

} // namespace LX_infra::image
