#include "vulkan_realtime_renderer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"
#include "core/asset/material_template.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_render_job.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include "infra/gui/gui.hpp"
#include "infra/image/rgba_image_io.hpp"
#include "infra/window/window.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/descriptors/descriptor_manager.hpp"
#include "details/device.hpp"
#include "details/device_resources/buffer.hpp"
#include "details/device_resources/texture.hpp"
#include "details/ibl_bake_renderer.hpp"
#include "details/render_objects/framebuffer.hpp"
#include "details/render_objects/render_pass.hpp"
#include "details/render_objects/swapchain.hpp"
#include "details/resource_manager.hpp"
#include "vulkan_post_process_builder.hpp"
#include "vulkan_renderer_foundation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace {
constexpr const char *kBloomBlurHShaderName = "bloom_blur_h";
constexpr const char *kBloomBlurVShaderName = "bloom_blur_v";

bool strictBindlessValidationEnabled() {
  return expEnvEnabled("LXE_STRICT_BINDLESS_VALIDATION");
}

bool isMigratedBindlessValidationPass(LX_core::StringID pass) {
  return pass == LX_core::Pass_Forward || pass == LX_core::Pass_Deferred;
}

LX_core::RenderPassNode makeDefaultRenderPathPass(
    LX_core::StringID passId, LX_core::ResourceUri shaderUri,
    std::vector<std::string> sources, std::vector<std::string> targets,
    LX_core::MaterialPassDispatch dispatch =
        LX_core::MaterialPassDispatch::Draw) {
  LX_core::RenderPassNode pass;
  pass.id = LX_core::GlobalStringTable::get().toDebugString(passId);
  pass.shaderUri = std::move(shaderUri);
  pass.stage = LX_core::MaterialPassStage::Raster;
  pass.dispatch = dispatch;
  pass.sources = std::move(sources);
  pass.targets = std::move(targets);
  return pass;
}

LX_core::RenderPathGraph makeDefaultForwardRenderPathGraph() {
  LX_core::RenderPathGraph graph;
  graph.name = "DefaultForward";
  graph.renderPath = LX_core::RenderPath::Forward;
  graph.features.push_back(LX_core::RenderPathFeatureDependency{
      "toneMapping", "effects/tone_mapping.render-feature.yaml"});
  graph.passes.push_back(makeDefaultRenderPathPass(
      LX_core::Pass_Forward, "techniques/Forward/pbr",
      {"camera.ubo", "geometry.vertex", "material.bsdf"},
      {"hdr.color", "depth.main"}));
  graph.passes.push_back(makeDefaultRenderPathPass(
      LX_core::Pass_PostProcess, "post_process",
      {"hdr.color", "feature.toneMapping"}, {"swapchain.color"},
      LX_core::MaterialPassDispatch::Fullscreen));
  return graph;
}

/// REQ-009: reverse of resource_manager.cpp's toVkFormat(ImageFormat).
/// Only covers the swapchain-relevant VkFormats. Unknown inputs fall back to
/// RGBA8 and log a debug warning rather than throwing — initScene must be
/// robust against whatever surface format the Vulkan driver exposes.
LX_core::ImageFormat toImageFormat(VkFormat format) {
  switch (format) {
  case VK_FORMAT_B8G8R8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_UNORM:
    return LX_core::ImageFormat::BGRA8;
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_R8G8B8A8_UNORM:
    return LX_core::ImageFormat::RGBA8;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return LX_core::ImageFormat::RGBA16Float;
  case VK_FORMAT_R8_UNORM:
    return LX_core::ImageFormat::R8;
  case VK_FORMAT_D32_SFLOAT:
    return LX_core::ImageFormat::D32Float;
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return LX_core::ImageFormat::D24UnormS8;
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return LX_core::ImageFormat::D32FloatS8;
  default:
    if (expRendererDebugEnabled()) {
      std::cerr << "[RendererDebug] toImageFormat: unknown VkFormat "
                << static_cast<int>(format) << ", falling back to RGBA8"
                << std::endl;
    }
    return LX_core::ImageFormat::RGBA8;
  }
}

VkFormat toVkFormat(LX_core::ImageFormat format) {
  switch (format) {
  case LX_core::ImageFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case LX_core::ImageFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case LX_core::ImageFormat::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case LX_core::ImageFormat::R8:
    return VK_FORMAT_R8_UNORM;
  case LX_core::ImageFormat::D32Float:
    return VK_FORMAT_D32_SFLOAT;
  case LX_core::ImageFormat::D24UnormS8:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case LX_core::ImageFormat::D32FloatS8:
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  }
  throw std::runtime_error("Unsupported ImageFormat");
}

std::string vkFormatName(VkFormat format) {
  switch (format) {
  case VK_FORMAT_D32_SFLOAT:
    return "D32_SFLOAT";
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return "D24_UNORM_S8_UINT";
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return "D32_SFLOAT_S8_UINT";
  case VK_FORMAT_R8G8B8A8_UNORM:
    return "R8G8B8A8_UNORM";
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return "R16G16B16A16_SFLOAT";
  case VK_FORMAT_R32G32B32A32_SFLOAT:
    return "R32G32B32A32_SFLOAT";
  case VK_FORMAT_B8G8R8A8_UNORM:
    return "B8G8R8A8_UNORM";
  default:
    return "VkFormat(" + std::to_string(static_cast<int>(format)) + ")";
  }
}

VkDeviceSize dumpByteSize(VkFormat format, u32 width, u32 height) {
  const VkDeviceSize pixelCount =
      static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height);
  switch (format) {
  case VK_FORMAT_D32_SFLOAT:
  case VK_FORMAT_R8G8B8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_UNORM:
    return pixelCount * 4u;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return pixelCount * 8u;
  case VK_FORMAT_R32G32B32A32_SFLOAT:
    return pixelCount * 16u;
  default:
    throw std::runtime_error("render debug dump does not support " +
                             vkFormatName(format));
  }
}

float halfToFloat(u16 value) {
  const u16 sign = static_cast<u16>((value >> 15u) & 0x1u);
  const u16 exponent = static_cast<u16>((value >> 10u) & 0x1fu);
  const u16 mantissa = static_cast<u16>(value & 0x03ffu);
  const float signScale = sign == 0 ? 1.0f : -1.0f;
  if (exponent == 0) {
    if (mantissa == 0) {
      return signScale * 0.0f;
    }
    return signScale * std::ldexp(static_cast<float>(mantissa), -24);
  }
  if (exponent == 31) {
    return mantissa == 0 ? signScale * std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
  }
  return signScale * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                static_cast<int>(exponent) - 15);
}

unsigned char linearToDebugByte(float value) {
  if (!std::isfinite(value)) {
    value = 0.0f;
  }
  value = std::max(value, 0.0f);
  const float mapped = value / (1.0f + value);
  return static_cast<unsigned char>(std::clamp(mapped, 0.0f, 1.0f) * 255.0f);
}

std::vector<unsigned char> makeBmpPixelsFromDump(VkFormat format, u32 width,
                                                 u32 height,
                                                 const void *mappedData) {
  std::vector<unsigned char> bgrPixels;
  bgrPixels.reserve(static_cast<usize>(width) * static_cast<usize>(height) *
                    3u);

  if (format == VK_FORMAT_D32_SFLOAT) {
    const auto *depthPixels = static_cast<const float *>(mappedData);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const float depth = std::clamp(
            depthPixels[static_cast<usize>(y) * width + x], 0.0f, 1.0f);
        const auto gray = static_cast<unsigned char>(depth * 255.0f);
        bgrPixels.push_back(gray);
        bgrPixels.push_back(gray);
        bgrPixels.push_back(gray);
      }
    }
    return bgrPixels;
  }

  if (format == VK_FORMAT_R8G8B8A8_UNORM ||
      format == VK_FORMAT_B8G8R8A8_UNORM) {
    const auto *rgba = static_cast<const unsigned char *>(mappedData);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const usize i =
            (static_cast<usize>(y) * width + static_cast<usize>(x)) * 4u;
        if (format == VK_FORMAT_B8G8R8A8_UNORM) {
          bgrPixels.push_back(rgba[i + 0u]);
          bgrPixels.push_back(rgba[i + 1u]);
          bgrPixels.push_back(rgba[i + 2u]);
        } else {
          bgrPixels.push_back(rgba[i + 2u]);
          bgrPixels.push_back(rgba[i + 1u]);
          bgrPixels.push_back(rgba[i + 0u]);
        }
      }
    }
    return bgrPixels;
  }

  if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
    const auto *pixels = static_cast<const u16 *>(mappedData);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const usize i =
            (static_cast<usize>(y) * width + static_cast<usize>(x)) * 4u;
        bgrPixels.push_back(linearToDebugByte(halfToFloat(pixels[i + 2u])));
        bgrPixels.push_back(linearToDebugByte(halfToFloat(pixels[i + 1u])));
        bgrPixels.push_back(linearToDebugByte(halfToFloat(pixels[i + 0u])));
      }
    }
    return bgrPixels;
  }

  if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
    const auto *pixels = static_cast<const float *>(mappedData);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const usize i =
            (static_cast<usize>(y) * width + static_cast<usize>(x)) * 4u;
        bgrPixels.push_back(linearToDebugByte(pixels[i + 2u]));
        bgrPixels.push_back(linearToDebugByte(pixels[i + 1u]));
        bgrPixels.push_back(linearToDebugByte(pixels[i + 0u]));
      }
    }
    return bgrPixels;
  }

  throw std::runtime_error("render debug dump does not support " +
                           vkFormatName(format));
}

struct DumpScalarStats final {
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  double nonZeroRatio = 0.0;
};

DumpScalarStats computeDumpScalarStats(VkFormat format, u32 width, u32 height,
                                       const void *mappedData) {
  const usize pixelCount = static_cast<usize>(width) * height;
  if (pixelCount == 0) {
    return {};
  }

  DumpScalarStats stats{
      .minValue = std::numeric_limits<double>::max(),
      .maxValue = 0.0,
      .meanValue = 0.0,
      .nonZeroRatio = 0.0,
  };
  usize nonZeroCount = 0;
  const auto pushScalar = [&](double value) {
    if (!std::isfinite(value)) {
      value = 0.0;
    }
    value = std::abs(value);
    stats.minValue = std::min(stats.minValue, value);
    stats.maxValue = std::max(stats.maxValue, value);
    stats.meanValue += value;
    if (value > 1.0e-6) {
      ++nonZeroCount;
    }
  };

  if (format == VK_FORMAT_D32_SFLOAT) {
    const auto *pixels = static_cast<const float *>(mappedData);
    for (usize i = 0; i < pixelCount; ++i) {
      pushScalar(pixels[i]);
    }
  } else if (format == VK_FORMAT_R8G8B8A8_UNORM ||
             format == VK_FORMAT_B8G8R8A8_UNORM) {
    const auto *pixels = static_cast<const unsigned char *>(mappedData);
    for (usize i = 0; i < pixelCount; ++i) {
      const usize base = i * 4u;
      const double r = pixels[base + (format == VK_FORMAT_B8G8R8A8_UNORM ? 2u : 0u)] / 255.0;
      const double g = pixels[base + 1u] / 255.0;
      const double b = pixels[base + (format == VK_FORMAT_B8G8R8A8_UNORM ? 0u : 2u)] / 255.0;
      pushScalar(std::max({r, g, b}));
    }
  } else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
    const auto *pixels = static_cast<const u16 *>(mappedData);
    for (usize i = 0; i < pixelCount; ++i) {
      const usize base = i * 4u;
      const double r = halfToFloat(pixels[base + 0u]);
      const double g = halfToFloat(pixels[base + 1u]);
      const double b = halfToFloat(pixels[base + 2u]);
      pushScalar(std::max({std::abs(r), std::abs(g), std::abs(b)}));
    }
  } else if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
    const auto *pixels = static_cast<const float *>(mappedData);
    for (usize i = 0; i < pixelCount; ++i) {
      const usize base = i * 4u;
      pushScalar(std::max({std::abs(static_cast<double>(pixels[base + 0u])),
                           std::abs(static_cast<double>(pixels[base + 1u])),
                           std::abs(static_cast<double>(pixels[base + 2u]))}));
    }
  } else {
    throw std::runtime_error("render debug stats does not support " +
                             vkFormatName(format));
  }

  stats.meanValue /= static_cast<double>(pixelCount);
  stats.nonZeroRatio = static_cast<double>(nonZeroCount) /
                       static_cast<double>(pixelCount);
  if (stats.minValue == std::numeric_limits<double>::max()) {
    stats.minValue = 0.0;
  }
  return stats;
}

LX_core::offline::OfflineReadbackImage
makeRgba32fImageFromDump(VkFormat format, u32 width, u32 height,
                         const void *mappedData) {
  LX_core::offline::OfflineReadbackImage image;
  image.width = width;
  image.height = height;
  image.rgba.resize(static_cast<usize>(width) * static_cast<usize>(height) *
                    4u);

  if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
    const auto *pixels = static_cast<const u16 *>(mappedData);
    for (usize i = 0; i < image.pixelCount() * 4u; ++i) {
      image.rgba[i] = halfToFloat(pixels[i]);
    }
    return image;
  }
  if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
    const auto *pixels = static_cast<const float *>(mappedData);
    std::copy(pixels, pixels + image.rgba.size(), image.rgba.begin());
    return image;
  }
  throw std::runtime_error("realtime profile output does not support " +
                           vkFormatName(format));
}

struct ProjectedBoundsDebug final {
  std::string nodeName;
  std::string objectSignature;
  u32 indexCount = 0;
  bool valid = false;
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
};

struct RealtimeProfileDebugInfo final {
  float profileAspect = 0.0f;
  float cameraAspect = 0.0f;
  u32 cameraResourceCount = 0;
  u32 lightResourceCount = 0;
  u32 drawItemCount = 0;
  VkExtent2D viewportExtent{};
  LX_core::Vec4f lightDirection{};
  LX_core::Mat4f cameraView = LX_core::Mat4f::identity();
  LX_core::Mat4f cameraProj = LX_core::Mat4f::identity();
  std::vector<ProjectedBoundsDebug> projectedBounds;
};

void writeMat4Json(std::ostream &out, const LX_core::Mat4f &matrix) {
  out << "[";
  for (int row = 0; row < 4; ++row) {
    if (row > 0) {
      out << ", ";
    }
    out << "[";
    for (int col = 0; col < 4; ++col) {
      if (col > 0) {
        out << ", ";
      }
      out << matrix(row, col);
    }
    out << "]";
  }
  out << "]";
}

[[nodiscard]] u32
countCameraResources(const LX_core::DescriptorResourceList &resources) {
  u32 count = 0;
  const LX_core::StringID cameraBinding("CameraUBO");
  for (const auto &resource : resources) {
    if (resource.getBindingName() == cameraBinding) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] u32
countLightResources(const LX_core::DescriptorResourceList &resources) {
  u32 count = 0;
  const LX_core::StringID lightBinding("LightUBO");
  for (const auto &resource : resources) {
    if (resource.getBindingName() == lightBinding) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] LX_core::Vec4f findLightDirection(
    const LX_core::DescriptorResourceList &resources) {
  const LX_core::StringID lightBinding("LightUBO");
  for (const auto &resource : resources) {
    if (!resource.isResource() || resource.getBindingName() != lightBinding ||
        resource.resource().get().getByteSize() <
            sizeof(LX_core::DirectionalLightData::Param)) {
      continue;
    }
    const auto *param =
        static_cast<const LX_core::DirectionalLightData::Param *>(
            resource.resource().get().getRawData());
    return param->dir;
  }
  return {};
}

[[nodiscard]] std::optional<LX_core::Mat4f>
extractModelMatrix(const LX_core::PerDrawDataSharedPtr &drawData) {
  if (!drawData || drawData->byteSize() < sizeof(LX_core::Mat4f)) {
    return std::nullopt;
  }
  LX_core::Mat4f model = LX_core::Mat4f::identity();
  std::memcpy(&model, drawData->rawData(), sizeof(model));
  return model;
}

[[nodiscard]] ProjectedBoundsDebug makeProjectedBoundsDebug(
    const LX_core::IRenderable &renderable, const LX_core::RenderWorkItem &item,
    const LX_core::Mat4f &viewProj, u32 width, u32 height) {
  ProjectedBoundsDebug out;
  out.nodeName = renderable.getNodeName();
  out.objectSignature =
      LX_core::GlobalStringTable::get().getName(item.objectSignature.id);
  out.indexCount = item.raster.indexBuffer.isValid()
                       ? item.raster.indexBuffer.get().getByteSize() /
                             sizeof(u32)
                       : 0;

  auto modelOpt = extractModelMatrix(item.raster.drawData);
  if (!modelOpt.has_value()) {
    return out;
  }
  const auto &sceneNode = dynamic_cast<const LX_core::SceneNode &>(renderable);
  const LX_core::BoundingBox localBounds = sceneNode.getLocalBounds();
  if (!localBounds.isValid()) {
    return out;
  }

  const LX_core::Mat4f model = *modelOpt;
  const LX_core::Mat4f mvp = viewProj * model;
  const LX_core::Vec4f corners[8] = {
      {localBounds.min.x, localBounds.min.y, localBounds.min.z, 1.0f},
      {localBounds.max.x, localBounds.min.y, localBounds.min.z, 1.0f},
      {localBounds.min.x, localBounds.max.y, localBounds.min.z, 1.0f},
      {localBounds.max.x, localBounds.max.y, localBounds.min.z, 1.0f},
      {localBounds.min.x, localBounds.min.y, localBounds.max.z, 1.0f},
      {localBounds.max.x, localBounds.min.y, localBounds.max.z, 1.0f},
      {localBounds.min.x, localBounds.max.y, localBounds.max.z, 1.0f},
      {localBounds.max.x, localBounds.max.y, localBounds.max.z, 1.0f},
  };

  float minX = std::numeric_limits<float>::infinity();
  float minY = std::numeric_limits<float>::infinity();
  float maxX = -std::numeric_limits<float>::infinity();
  float maxY = -std::numeric_limits<float>::infinity();
  for (const auto &corner : corners) {
    const LX_core::Vec4f clip = mvp * corner;
    if (std::abs(clip.w) <= std::numeric_limits<float>::epsilon()) {
      continue;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
    const float py = (ndcY * 0.5f + 0.5f) * static_cast<float>(height);
    minX = std::min(minX, px);
    minY = std::min(minY, py);
    maxX = std::max(maxX, px);
    maxY = std::max(maxY, py);
  }
  if (std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) &&
      std::isfinite(maxY)) {
    out.valid = true;
    out.minX = minX;
    out.minY = minY;
    out.maxX = maxX;
    out.maxY = maxY;
  }
  return out;
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20u) {
        constexpr char kHex[] = "0123456789ABCDEF";
        out += "\\u00";
        out.push_back(kHex[(c >> 4u) & 0x0fu]);
        out.push_back(kHex[c & 0x0fu]);
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return out;
}

void writeRealtimeProfileMetadata(
    const std::filesystem::path &path,
    const LX_core::backend::VulkanRealtimeProfileOutputResult &result,
    std::string_view pipelineStatus,
    const RealtimeProfileDebugInfo &debugInfo) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    throw std::runtime_error("failed to open realtime profile metadata " +
                             path.string());
  }
  out << "{\n"
      << "  \"width\": " << result.width << ",\n"
      << "  \"height\": " << result.height << ",\n"
      << "  \"linearExrPath\": \""
      << jsonEscape(result.linearExrPath.generic_string()) << "\",\n"
      << "  \"cpuSrgbPngPath\": \""
      << jsonEscape(result.cpuSrgbPngPath.generic_string()) << "\",\n"
      << "  \"pipelineSrgbPngPath\": \""
      << jsonEscape(result.pipelineSrgbPngPath.generic_string()) << "\",\n"
      << "  \"depthDebugPath\": \""
      << jsonEscape(result.depthDebugPath.generic_string()) << "\",\n"
      << "  \"pipelineSrgbStatus\": \"" << jsonEscape(pipelineStatus) << "\",\n"
      << "  \"debug\": {\n"
      << "    \"profileAspect\": " << debugInfo.profileAspect << ",\n"
      << "    \"cameraAspect\": " << debugInfo.cameraAspect << ",\n"
      << "    \"cameraResourceCount\": " << debugInfo.cameraResourceCount
      << ",\n"
      << "    \"lightResourceCount\": " << debugInfo.lightResourceCount << ",\n"
      << "    \"lightDirection\": [" << debugInfo.lightDirection.x << ", "
      << debugInfo.lightDirection.y << ", " << debugInfo.lightDirection.z
      << ", " << debugInfo.lightDirection.w << "],\n"
      << "    \"drawItemCount\": " << debugInfo.drawItemCount << ",\n"
      << "    \"viewportExtent\": {\"width\": "
      << debugInfo.viewportExtent.width
      << ", \"height\": " << debugInfo.viewportExtent.height << "},\n"
      << "    \"cameraView\": ";
  writeMat4Json(out, debugInfo.cameraView);
  out << ",\n"
      << "    \"cameraProj\": ";
  writeMat4Json(out, debugInfo.cameraProj);
  out << ",\n"
      << "    \"projectedBounds\": [\n";
  for (usize i = 0; i < debugInfo.projectedBounds.size(); ++i) {
    const auto &bounds = debugInfo.projectedBounds[i];
    out << "      {\"node\": \"" << jsonEscape(bounds.nodeName)
        << "\", \"objectSignature\": \"" << jsonEscape(bounds.objectSignature)
        << "\", \"indexCount\": " << bounds.indexCount
        << ", \"valid\": " << (bounds.valid ? "true" : "false")
        << ", \"minX\": " << bounds.minX << ", \"minY\": " << bounds.minY
        << ", \"maxX\": " << bounds.maxX << ", \"maxY\": " << bounds.maxY
        << "}";
    if (i + 1 < debugInfo.projectedBounds.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ]\n"
      << "  }\n"
      << "}\n";
}

VkPipelineStageFlags dumpRestoreStage(VkImageLayout layout,
                                      VkImageAspectFlags aspect) {
  if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    return VK_PIPELINE_STAGE_TRANSFER_BIT;
  }
  return (aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0
             ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
             : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

VkAccessFlags dumpRestoreAccess(VkImageLayout layout,
                                VkImageAspectFlags aspect) {
  if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    return VK_ACCESS_SHADER_READ_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    return VK_ACCESS_TRANSFER_READ_BIT;
  }
  return (aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0
             ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
             : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
}

std::string sanitizeAttachmentName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
    out.push_back(safe ? c : '_');
  }
  return out.empty() ? "attachment" : out;
}

LX_core::StringID passIdFromDebugName(std::string_view passName) {
  if (passName == "Forward" || passName == "forward") {
    return LX_core::Pass_Forward;
  }
  if (passName == "BloomThreshold" || passName == "bloomThreshold" ||
      passName == "bloom_threshold") {
    return LX_core::Pass_BloomThreshold;
  }
  if (passName == "BloomBlurH" || passName == "bloomBlurH" ||
      passName == "bloom_blur_h") {
    return LX_core::Pass_BloomBlurH;
  }
  if (passName == "BloomBlurV" || passName == "bloomBlurV" ||
      passName == "bloom_blur_v") {
    return LX_core::Pass_BloomBlurV;
  }
  if (passName == "PostProcess" || passName == "postProcess" ||
      passName == "post_process") {
    return LX_core::Pass_PostProcess;
  }
  if (passName == "DebugOverlay" || passName == "debugOverlay" ||
      passName == "debug_overlay") {
    return LX_core::Pass_DebugOverlay;
  }
  throw std::runtime_error("unsupported debug render target pass: " +
                           std::string(passName));
}

void writeLe16(std::ostream &out, u16 value) {
  out.put(static_cast<char>(value & 0xffu));
  out.put(static_cast<char>((value >> 8u) & 0xffu));
}

void writeLe32(std::ostream &out, u32 value) {
  out.put(static_cast<char>(value & 0xffu));
  out.put(static_cast<char>((value >> 8u) & 0xffu));
  out.put(static_cast<char>((value >> 16u) & 0xffu));
  out.put(static_cast<char>((value >> 24u) & 0xffu));
}

void writeBmp24Header(std::ostream &out, u32 width, u32 height,
                      u32 pixelBytes) {
  constexpr u32 fileHeaderBytes = 14;
  constexpr u32 dibHeaderBytes = 40;
  writeLe16(out, 0x4d42u);
  writeLe32(out, fileHeaderBytes + dibHeaderBytes + pixelBytes);
  writeLe16(out, 0);
  writeLe16(out, 0);
  writeLe32(out, fileHeaderBytes + dibHeaderBytes);

  writeLe32(out, dibHeaderBytes);
  writeLe32(out, width);
  writeLe32(out, height);
  writeLe16(out, 1);
  writeLe16(out, 24);
  writeLe32(out, 0);
  writeLe32(out, pixelBytes);
  writeLe32(out, 2835);
  writeLe32(out, 2835);
  writeLe32(out, 0);
  writeLe32(out, 0);
}

void writeBmp24File(const std::filesystem::path &path, u32 width, u32 height,
                    const std::vector<unsigned char> &bgrPixels) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  const u32 rowBytes = width * 3u;
  const u32 paddedRowBytes = (rowBytes + 3u) & ~3u;
  const u32 paddingBytes = paddedRowBytes - rowBytes;
  const u32 pixelBytes = paddedRowBytes * height;

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("failed to open render target dump file: " +
                             path.string());
  }
  writeBmp24Header(out, width, height, pixelBytes);
  for (u32 y = 0; y < height; ++y) {
    const u32 srcY = height - 1u - y;
    const usize rowStart = static_cast<usize>(srcY) * width * 3u;
    for (u32 x = 0; x < rowBytes; ++x) {
      out.put(static_cast<char>(bgrPixels[rowStart + x]));
    }
    for (u32 p = 0; p < paddingBytes; ++p) {
      out.put('\0');
    }
  }
  if (!out) {
    throw std::runtime_error("failed to write render target dump file: " +
                             path.string());
  }
}
} // namespace

namespace LX_core::backend {

namespace {

constexpr u32 kMaxFramesInFlight = 3;

bool isDirtyHostBufferResource(const GpuResourceRef &resource) {
  if (!resource.isValid() || !resource.get().isDirty()) {
    return false;
  }

  switch (resource.get().getType()) {
  case ResourceType::VertexBuffer:
  case ResourceType::IndexBuffer:
  case ResourceType::UniformBuffer:
  case ResourceType::StorageBuffer:
    return true;
  default:
    return false;
  }
}

VkImageLayout attachmentWriteLayout(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color
             ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
             : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

VkPipelineStageFlags attachmentWriteStage(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color
             ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
             : (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

VkAccessFlags attachmentWriteAccess(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color
             ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
             : (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

VkImageAspectFlags attachmentAspect(FrameGraphAttachmentKind kind) {
  return kind == FrameGraphAttachmentKind::Color ? VK_IMAGE_ASPECT_COLOR_BIT
                                                 : VK_IMAGE_ASPECT_DEPTH_BIT;
}

std::optional<std::reference_wrapper<const LX_core::FrameGraphWrite>>
findWriteForKind(const LX_core::CompiledFrameGraphPass &pass,
                 LX_core::FrameGraphAttachmentKind kind) {
  std::optional<std::reference_wrapper<const LX_core::FrameGraphWrite>> found;
  for (const auto &write : pass.writes) {
    if (write.resource.kind != kind) {
      continue;
    }
    if (found.has_value()) {
      throw std::runtime_error(
          "Frame graph pass declares duplicate writes for one attachment kind");
    }
    found = std::cref(write);
  }
  return found;
}

std::vector<std::reference_wrapper<const LX_core::FrameGraphWrite>>
findWritesForKind(const LX_core::CompiledFrameGraphPass &pass,
                  LX_core::FrameGraphAttachmentKind kind) {
  std::vector<std::reference_wrapper<const LX_core::FrameGraphWrite>> found;
  for (const auto &write : pass.writes) {
    if (write.resource.kind == kind) {
      found.push_back(std::cref(write));
    }
  }
  return found;
}

void validateOffscreenWritesMatchTarget(
    const LX_core::CompiledFrameGraphPass &pass) {
  const auto colorWrites =
      findWritesForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
  const auto depthWrite =
      findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Depth);

  if (pass.target.colorAttachmentCount() != colorWrites.size()) {
    throw std::runtime_error(
        "Frame graph offscreen pass color write does not match target: " +
        LX_core::GlobalStringTable::get().getName(pass.name.id));
  }
  if (pass.target.depthFormat.has_value() != depthWrite.has_value()) {
    throw std::runtime_error(
        "Frame graph offscreen pass depth write does not match target: " +
        LX_core::GlobalStringTable::get().getName(pass.name.id));
  }
}

} // namespace

class VulkanRealtimeRenderer::Impl {
public:
  Impl() = default;
  ~Impl() { destroy(); }

  void initialize(WindowSharedPtr _window, const char *appName) {
    m_window = _window;

    m_foundation = VulkanRendererFoundation::createRealtime(_window, appName);
    resourceManager().initializeRenderPassAndPipeline(
        device().getSurfaceFormat(), device().getDepthFormat());
    if (expEnvEnabled("LX_RENDER_DEBUG_CLEAR")) {
      resourceManager().getRenderPass().setClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    }

    m_swapchain =
        VulkanSwapchain::create(device(), _window, kMaxFramesInFlight);
    m_swapchain->initialize(resourceManager().getRenderPass());

    // REQ-017: bring up ImGui overlay inside the swapchain render pass.
    infra::Gui::InitParams guiParams{};
    guiParams.instance = device().getInstance();
    guiParams.physicalDevice = device().getPhysicalDevice();
    guiParams.device = device().getLogicalDevice();
    guiParams.graphicsQueueFamilyIndex = device().getGraphicsQueueFamilyIndex();
    guiParams.presentQueueFamilyIndex = device().getPresentQueueFamilyIndex();
    guiParams.graphicsQueue = device().getGraphicsQueue();
    guiParams.presentQueue = device().getPresentQueue();
    guiParams.surface = device().getSurface();
    guiParams.nativeWindowHandle = _window->getNativeHandle();
    guiParams.renderPass = resourceManager().getRenderPass().getHandle();
    guiParams.swapchainImageCount = m_swapchain->getImageCount();
    m_gui.init(guiParams);
  }
  void shutdown() { destroy(); }
  void setPostProcessSettings(const VulkanPostProcessSettings &settings) {
    m_postProcessSettings = settings;
  }
  [[nodiscard]] const VulkanPostProcessSettings &postProcessSettings() const {
    return m_postProcessSettings;
  }

  [[nodiscard]] VulkanDevice &device() { return m_foundation->device(); }
  [[nodiscard]] const VulkanDevice &device() const {
    return m_foundation->device();
  }
  [[nodiscard]] VulkanResourceManager &resourceManager() {
    return m_foundation->resourceManager();
  }
  [[nodiscard]] const VulkanResourceManager &resourceManager() const {
    return m_foundation->resourceManager();
  }
  [[nodiscard]] VulkanCommandBufferManager &commandBufferManager() {
    return m_foundation->commandBufferManager();
  }

  void syncRenderUploadPlan(const LX_core::RenderWorkQueue &queue) {
    const LX_core::RenderUploadPlan uploadPlan =
        LX_core::buildRenderUploadPlan(queue);
    for (const auto &resource : uploadPlan.resources) {
      resourceManager().syncResource(commandBufferManager(), resource);
    }
  }

  [[nodiscard]] bool uploadPlanRequiresSharedHostBufferSync(
      const LX_core::RenderWorkQueue &queue) const {
    const LX_core::RenderUploadPlan uploadPlan =
        LX_core::buildRenderUploadPlan(queue);
    for (const auto &resource : uploadPlan.resources) {
      if (isDirtyHostBufferResource(resource)) {
        return true;
      }
    }
    return false;
  }

  /// REQ-009: derive the real swapchain RenderTarget from the Vulkan device's
  /// chosen surface format + depth format. This is the value that gets plugged
  /// into FramePass.target and also backfilled into any Camera whose m_target
  /// is nullopt at initScene time.
  LX_core::RenderTarget makeSwapchainTarget() const {
    LX_core::RenderTarget t{};
    t.colorFormat = toImageFormat(device().getSurfaceFormat().format);
    t.depthFormat = toImageFormat(device().getDepthFormat());
    t.sampleCount = 1;
    return t;
  }

  void bakeSceneIblEnvironmentIfNeeded() {
    if (!m_scene) {
      return;
    }
    auto *resources = m_scene->resources().getMutableIblEnvironmentResources();
    if (resources == nullptr || !resources->equirectangularMap ||
        resources->bakedSkyboxCubemap) {
      return;
    }

    LX_core::backend::IblBakeRenderer baker(device(), resourceManager(),
                                            commandBufferManager());
    const u32 prefilterMipCount = std::max(
        1u, static_cast<u32>(std::round(
                resources->environmentUbo
                    ? resources->environmentUbo->getPrefilteredMipCount()
                    : 1.0f)));
    auto baked = baker.bakeStaticEnvironment({
        .equirectangularMap = resources->equirectangularMap,
        .skyboxSize = 64,
        .irradianceSize = 32,
        .prefilterSize = 64,
        .prefilterMipCount = prefilterMipCount,
        .brdfLutSize = 128,
    });

    resources->bakedSkyboxCubemap = std::move(baked.skybox);
    resources->bakedIrradianceCubemap = std::move(baked.irradiance);
    resources->bakedPrefilteredRadianceCubemap = std::move(baked.prefiltered);
    resources->bakedBrdfLut = std::move(baked.brdfLut);
  }

  void initScene(SceneSharedPtr _scene) {
    ++m_initSceneCallCount;
    if (m_swapchain) {
      m_swapchain->waitForAllFrames();
    }
    m_scene = _scene;

    const LX_core::RenderTarget swapchainTarget = makeSwapchainTarget();
    const auto swapchainDesc = swapchainTarget.toDesc();
    LX_core::RenderTargetDesc forwardHdrDesc;
    forwardHdrDesc.role = LX_core::RenderTargetRole::Offscreen;
    forwardHdrDesc.colorFormat = LX_core::ImageFormat::RGBA16Float;
    forwardHdrDesc.depthFormat = swapchainTarget.depthFormat;
    const auto shadowTarget =
        LX_core::RenderTargetDesc::offscreenDepth(swapchainTarget.depthFormat);

    updateDirectionalLightCascades();
    bakeSceneIblEnvironmentIfNeeded();
    auto &forwardRenderPass = resourceManager().getRenderPass(forwardHdrDesc);
    forwardRenderPass.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    const auto sceneHdrColor = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("hdr.color"));
    const auto sceneDepth = LX_core::FrameGraphResourceRef::depthAttachment(
        LX_core::StringID("depth.main"));
    const auto gbufferAlbedoAlpha =
        LX_core::FrameGraphResourceRef::colorAttachment(
            LX_core::StringID("gbuffer.albedoAlpha"));
    const auto gbufferNormalRoughness =
        LX_core::FrameGraphResourceRef::colorAttachment(
            LX_core::StringID("gbuffer.normalRoughness"));
    const auto gbufferMaterial =
        LX_core::FrameGraphResourceRef::colorAttachment(
            LX_core::StringID("gbuffer.material"));
    const auto bloomThreshold = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("bloom.threshold"));
    const auto bloomBlurH = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("bloom.blurH"));
    const auto bloomBlur = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("bloom.blur"));
    const auto swapchainColor = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("swapchain.color"));

    m_frameGraph = LX_core::FrameGraph{}; // Fresh graph on every initScene.
    m_scene->resources().beginRenderResourceScope();
    std::vector<LX_core::FrameGraphRead> shadowReads;
    if (m_scene->renderSettings().shadows) {
      shadowReads.reserve(LX_core::MaxShadowCascades);
      for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
           ++cascadeIndex) {
        const auto shadowDepth =
            LX_core::FrameGraphResourceRef::depthAttachment(LX_core::StringID(
                "shadow.cascade" + std::to_string(cascadeIndex)));
        m_frameGraph.addPass(
            LX_core::FramePass{LX_core::Pass_Shadow,
                               shadowTarget,
                               {},
                               {},
                               {LX_core::FrameGraphWrite{shadowDepth}}});
        shadowReads.push_back(LX_core::FrameGraphRead::sampled(
            shadowDepth.name,
            LX_core::StringID("ShadowMap" + std::to_string(cascadeIndex))));
      }
    }
    const bool deferredMode =
        m_scene->realtimeRenderSettings().mode ==
        LX_core::SceneRealtimeRenderMode::Deferred;
    LX_core::RenderTargetDesc gbufferDesc =
        LX_core::RenderTargetDesc::offscreenColors(
            {LX_core::ImageFormat::RGBA16Float,
             LX_core::ImageFormat::RGBA16Float,
             LX_core::ImageFormat::RGBA16Float},
            swapchainTarget.depthFormat);
    const auto deferredLightingDesc =
        LX_core::RenderTargetDesc::offscreenColor(
            LX_core::ImageFormat::RGBA16Float);
    if (deferredMode) {
      m_frameGraph.addPass(
          LX_core::FramePass{LX_core::Pass_Deferred,
                             gbufferDesc,
                             {},
                             {},
                             {LX_core::FrameGraphWrite{gbufferAlbedoAlpha},
                              LX_core::FrameGraphWrite{gbufferNormalRoughness},
                              LX_core::FrameGraphWrite{gbufferMaterial},
                              LX_core::FrameGraphWrite{sceneDepth}}});
      m_frameGraph.addPass(LX_core::FramePass{
          LX_core::Pass_DeferredLighting,
          deferredLightingDesc,
          {},
          {LX_core::FrameGraphRead::sampled(
               gbufferAlbedoAlpha.name,
               LX_core::StringID("GBufferAlbedoAlpha")),
           LX_core::FrameGraphRead::sampled(
               gbufferNormalRoughness.name,
               LX_core::StringID("GBufferNormalRoughness")),
           LX_core::FrameGraphRead::sampled(gbufferMaterial.name,
                                            LX_core::StringID("GBufferMaterial")),
           LX_core::FrameGraphRead::sampled(sceneDepth.name,
                                            LX_core::StringID("GBufferDepth"))},
          {LX_core::FrameGraphWrite{sceneHdrColor}}});
    } else {
      LX_core::FrameGraph defaultForwardGraph =
          LX_core::buildFrameGraphFromRenderPathGraph(
              makeDefaultForwardRenderPathGraph(),
              LX_core::GraphResourceRegistry::makeDefault());
      for (auto pass : defaultForwardGraph.getPasses()) {
        if (pass.name == LX_core::Pass_PostProcess &&
            m_postProcessSettings.bloomEnabled) {
          continue;
        }
        if (pass.name == LX_core::Pass_Forward) {
          pass.target = forwardHdrDesc;
          pass.reads.insert(pass.reads.end(), shadowReads.begin(),
                            shadowReads.end());
        } else if (pass.name == LX_core::Pass_PostProcess) {
          pass.target = swapchainDesc;
          pass.reads = {
              LX_core::FrameGraphRead::sampled(sceneHdrColor.name,
                                               LX_core::StringID("SceneColor")),
              LX_core::FrameGraphRead::sampled(sceneHdrColor.name,
                                               LX_core::StringID("BloomColor")),
          };
        }
        m_frameGraph.addPass(std::move(pass));
      }
    }
    if (m_postProcessSettings.bloomEnabled) {
      m_frameGraph.addPass(LX_core::FramePass{
          LX_core::Pass_BloomThreshold,
          LX_core::RenderTargetDesc::offscreenColor(
              LX_core::ImageFormat::RGBA16Float),
          {},
          {LX_core::FrameGraphRead::sampled(sceneHdrColor.name,
                                            LX_core::StringID("SceneColor"))},
          {LX_core::FrameGraphWrite{bloomThreshold}}});
      m_frameGraph.addPass(LX_core::FramePass{
          LX_core::Pass_BloomBlurH,
          LX_core::RenderTargetDesc::offscreenColor(
              LX_core::ImageFormat::RGBA16Float),
          {},
          {LX_core::FrameGraphRead::sampled(bloomThreshold.name,
                                            LX_core::StringID("BloomSource"))},
          {LX_core::FrameGraphWrite{bloomBlurH}}});
      m_frameGraph.addPass(LX_core::FramePass{
          LX_core::Pass_BloomBlurV,
          LX_core::RenderTargetDesc::offscreenColor(
              LX_core::ImageFormat::RGBA16Float),
          {},
          {LX_core::FrameGraphRead::sampled(bloomBlurH.name,
                                            LX_core::StringID("BloomSource"))},
          {LX_core::FrameGraphWrite{bloomBlur}}});
    }
    if (deferredMode || m_postProcessSettings.bloomEnabled) {
      const auto postBloomInput = m_postProcessSettings.bloomEnabled
                                      ? bloomBlur.name
                                      : sceneHdrColor.name;
      m_frameGraph.addPass(LX_core::FramePass{
          LX_core::Pass_PostProcess,
          swapchainDesc,
          {},
          {LX_core::FrameGraphRead::sampled(sceneHdrColor.name,
                                            LX_core::StringID("SceneColor")),
           LX_core::FrameGraphRead::sampled(postBloomInput,
                                            LX_core::StringID("BloomColor"))},
          {LX_core::FrameGraphWrite{swapchainColor}}});
    }
    LX_core::FramePass debugOverlayPass{
        LX_core::Pass_DebugOverlay, swapchainDesc, {}, {}, {}};
    debugOverlayPass.phase = LX_core::FrameGraphPhase::Debug;
    m_frameGraph.addPass(std::move(debugOverlayPass));

    // RenderWorkQueue::build (invoked per pass below) internally:
    //   - filters renderables by supportsPass(pass)
    //   - merges scene.getSceneLevelResources(pass, target) (camera UBO
    //   filtered by
    //     target, light UBO filtered by pass mask)
    //   - sorts by PipelineKey
    // There is no more side-channel camera/light UBO injection here.
    m_frameGraph.build(LX_core::RenderWorkBuildContext::realtime(*m_scene));
    if (deferredMode) {
      rebuildPassQueueWithDefaultCameraResources(LX_core::Pass_Deferred,
                                                 gbufferDesc);
      addDeferredLightingItem(deferredLightingDesc);
    } else {
      rebuildPassQueueWithDefaultCameraResources(LX_core::Pass_Forward,
                                                 forwardHdrDesc);
      addSkyboxBackgroundItem(forwardHdrDesc);
    }
    rebuildDebugOverlayQueueWithDefaultCameraResources(swapchainDesc);
    if (m_postProcessSettings.bloomEnabled) {
      addBloomThresholdItem();
      addBloomBlurItem(LX_core::Pass_BloomBlurH, kBloomBlurHShaderName,
                       "BloomBlurHFullscreenTriangle");
      addBloomBlurItem(LX_core::Pass_BloomBlurV, kBloomBlurVShaderName,
                       "BloomBlurVFullscreenTriangle");
    }
    addStandardPostProcessItem(swapchainDesc);
    rebuildShadowCascadeUboSnapshots();
    bindShadowCascadeUboSnapshots();

    m_compiledFrameGraph = m_frameGraph.compile();
    if (!m_compiledFrameGraph.isValid()) {
      throw std::runtime_error(m_compiledFrameGraph.errorText());
    }
    attachFrameGraphSampledResources();
    resetOffscreenFramebuffers();
    resourceManager().clearFrameGraphAttachments();

    // Initial resource sync for every item across every pass in the FrameGraph.
    // SceneNode::getValidatedPassData() has already synced each per-draw model
    // matrix from the node world transform while building the queue.
    for (auto &pass : m_frameGraph.getPasses()) {
      syncRenderUploadPlan(pass.queue);
    }
    resourceManager().collectGarbage();

    // Pre-build every pipeline the scene needs. Runtime cache misses still
    // work via getOrCreatePipeline(item) but emit a warning log.
    auto infos = m_frameGraph.collectAllPipelineBuildDescs();
    resourceManager().preloadPipelines(infos);
  }

  void uploadData() {
    updateDirectionalLightCascades();

    const u32 currentFrameIndex = m_frameIndex % kMaxFramesInFlight;
    resourceManager().beginFrame(currentFrameIndex);
    bool requiresSharedBufferSync = false;
    for (auto &pass : m_frameGraph.getPasses()) {
      requiresSharedBufferSync =
          requiresSharedBufferSync ||
          uploadPlanRequiresSharedHostBufferSync(pass.queue);
      if (requiresSharedBufferSync) {
        break;
      }
    }

    if (requiresSharedBufferSync) {
      // These buffers are single shared allocations, not per-frame slices.
      // Wait until every in-flight frame that could still read them has
      // completed before overwriting their contents from the CPU.
      m_swapchain->waitForAllFrames();
    }

    for (auto &pass : m_frameGraph.getPasses()) {
      syncRenderUploadPlan(pass.queue);
    }
    resourceManager().collectGarbage();
  }

  void draw() {
    if (m_swapchainNeedsRebuild) {
      rebuildSwapchain();
      return;
    }

    // If the window has zero client area (minimized or in the middle of a
    // drag-resize on Windows), rebuilding or acquiring would either fail or
    // produce an invalid swapchain. Skip this frame cleanly; the next call
    // will retry once the window has non-zero size again.
    if (m_window && (m_window->getWidth() <= 0 || m_window->getHeight() <= 0)) {
      return;
    }

    const VkExtent2D extent = m_swapchain->getExtent();

    const u32 currentFrameIndex = m_frameIndex % kMaxFramesInFlight;
    u32 imageIndex = 0;

    VkResult acquireResult =
        m_swapchain->acquireNextImage(currentFrameIndex, imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
        acquireResult == VK_SUBOPTIMAL_KHR) {
      m_swapchainNeedsRebuild = true;
      // No queue submission will happen on this path, so keep the frame fence
      // signaled. Resetting it here would leave the next acquire blocked if
      // swapchain rebuild is deferred while the window is zero-sized.
      rebuildSwapchain();
      return;
    }
    if (acquireResult != VK_SUCCESS) {
      std::cerr
          << "[VulkanRenderer] vkAcquireNextImageKHR failed with VkResult="
          << static_cast<int>(acquireResult) << std::endl;
      return;
    }

    commandBufferManager().beginFrame(currentFrameIndex);
    device().getDescriptorManager().beginFrame(currentFrameIndex);
    resourceManager().beginFrame(currentFrameIndex);

    auto cmd = commandBufferManager().allocateBuffer();
    cmd->begin();

    const bool skipGuiFrame = expEnvEnabled("LX_RENDER_SKIP_GUI_FRAME") ||
                              m_pendingScreenDump.has_value();

    bool swapchainRenderPassActive = false;
    bool guiFrameActive = false;
    const usize finalSwapchainPassIndex = findFinalSwapchainPassIndex();
    const usize finalSwapchainGroupStartIndex =
        findFinalSwapchainGroupStartIndex(finalSwapchainPassIndex);

    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
      const auto &compiledPass = compiledPasses[passIndex];
      if (compiledPass.target.role == LX_core::RenderTargetRole::Swapchain) {
        const bool isFinalSwapchainGroup =
            finalSwapchainPassIndex != compiledPasses.size() &&
            passIndex >= finalSwapchainGroupStartIndex &&
            passIndex <= finalSwapchainPassIndex;
        if (!swapchainRenderPassActive) {
          auto &renderPass = resourceManager().getRenderPass();
          cmd->beginRenderPass(
              renderPass.getHandle(),
              m_swapchain->getFramebuffer(imageIndex).getHandle(), extent,
              renderPass.getClearValues());
          cmd->setViewport(extent.width, extent.height);
          cmd->setScissor(extent.width, extent.height);
          swapchainRenderPassActive = true;
        }

        if (isFinalSwapchainGroup &&
            passIndex == finalSwapchainGroupStartIndex && !skipGuiFrame) {
          m_gui.beginFrame();
          guiFrameActive = true;
          if (m_drawUiCallback) {
            m_drawUiCallback();
          }
        }

        drawPassQueue(passIndex, *cmd);

        if (!isFinalSwapchainGroup || passIndex == finalSwapchainPassIndex) {
          if (guiFrameActive) {
            m_gui.endFrame(cmd->getHandle());
            guiFrameActive = false;
          }
          cmd->endRenderPass();
          swapchainRenderPassActive = false;
        }
        continue;
      }

      if (swapchainRenderPassActive) {
        if (guiFrameActive) {
          m_gui.endFrame(cmd->getHandle());
          guiFrameActive = false;
        }
        cmd->endRenderPass();
        swapchainRenderPassActive = false;
      }

      prepareShadowCascadePass(passIndex);
      const VkExtent2D passExtent = prepareOffscreenPass(
          passIndex, currentFrameIndex, compiledPass, extent, *cmd);
      auto &renderPass = resourceManager().getRenderPass(compiledPass.target);
      cmd->beginRenderPass(
          renderPass.getHandle(),
          m_offscreenFramebuffers[passIndex][currentFrameIndex]->getHandle(),
          passExtent, renderPass.getClearValues());
      cmd->setViewport(passExtent.width, passExtent.height);
      cmd->setScissor(passExtent.width, passExtent.height);
      drawPassQueue(passIndex, *cmd);
      cmd->endRenderPass();
      transitionPassWritesToShaderRead(compiledPass, *cmd);
    }

    if (swapchainRenderPassActive) {
      if (guiFrameActive) {
        m_gui.endFrame(cmd->getHandle());
      }
      cmd->endRenderPass();
    }
    recordPendingScreenDump(imageIndex, extent, *cmd);
    cmd->end();

    VkSemaphore waitSemaphores[] = {
        m_swapchain->getImageAvailableSemaphore(currentFrameIndex)};
    VkSemaphore signalSemaphores[] = {
        m_swapchain->getRenderFinishedSemaphore(imageIndex)};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    VkCommandBuffer handle = cmd->getHandle();
    submitInfo.pCommandBuffers = &handle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkFence fence = m_swapchain->getInFlightFence(currentFrameIndex);
    vkResetFences(device().getLogicalDevice(), 1, &fence);
    const VkResult submitResult =
        vkQueueSubmit(device().getGraphicsQueue(), 1, &submitInfo, fence);
    if (submitResult != VK_SUCCESS) {
      consumeAcquireSemaphoreAndSignalFenceAfterFailedSubmit(
          waitSemaphores[0], waitStages[0], fence);
      std::cerr << "[VulkanRenderer] vkQueueSubmit failed with VkResult="
                << static_cast<int>(submitResult) << std::endl;
      return;
    }

    writeCompletedScreenDumpIfNeeded(fence);

    VkResult presentResult = m_swapchain->present(imageIndex);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
      m_swapchainNeedsRebuild = true;
      rebuildSwapchain();
      return;
    }
    if (presentResult != VK_SUCCESS) {
      std::cerr << "[VulkanRenderer] vkQueuePresentKHR failed with VkResult="
                << static_cast<int>(presentResult) << std::endl;
    }

    m_frameIndex++;
  }

  void setDrawUiCallback(std::function<void()> cb) {
    m_drawUiCallback = std::move(cb);
  }

  [[nodiscard]] usize cachedResourceCount() const {
    return m_foundation ? resourceManager().getCachedResourceCount() : 0;
  }

  [[nodiscard]] usize frameGraphItemCount() const {
    usize total = 0;
    for (const auto &pass : m_frameGraph.getPasses()) {
      total += pass.queue.getItems().size();
    }
    return total;
  }

  [[nodiscard]] usize compiledFrameGraphPassCount() const {
    return m_compiledFrameGraph.getPasses().size();
  }

  [[nodiscard]] std::vector<std::string> compiledFrameGraphPassNames() const {
    std::vector<std::string> names;
    const auto &passes = m_compiledFrameGraph.getPasses();
    names.reserve(passes.size());
    for (const auto &pass : passes) {
      names.push_back(LX_core::GlobalStringTable::get().getName(pass.name.id));
    }
    return names;
  }

  [[nodiscard]] usize frameGraphAttachmentCount() const {
    return m_foundation ? resourceManager().getFrameGraphAttachmentCount() : 0;
  }

  [[nodiscard]] usize initSceneCallCount() const {
    return m_initSceneCallCount;
  }

  VulkanFrameGraphAttachmentDumpResult dumpFrameGraphAttachment(
      std::string_view attachmentName,
      const std::optional<std::filesystem::path> &requestedPath,
      const std::optional<std::filesystem::path> &requestedScreenPath) {
    if (!m_foundation) {
      throw std::runtime_error("renderer is not initialized");
    }

    const StringID attachmentId{std::string(attachmentName)};
    auto attachmentOpt =
        resourceManager().getFrameGraphAttachment(attachmentId);
    if (!attachmentOpt.has_value()) {
      throw std::runtime_error("frame graph attachment not available: " +
                               std::string(attachmentName));
    }
    auto &attachment = attachmentOpt->get();

    const u32 width = attachment.extent.width;
    const u32 height = attachment.extent.height;
    const VkDeviceSize byteSize =
        dumpByteSize(attachment.format, width, height);
    if (width == 0 || height == 0 || byteSize == 0) {
      throw std::runtime_error("frame graph attachment has empty extent: " +
                               std::string(attachmentName));
    }

    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const std::filesystem::path path = requestedPath.value_or(
        std::filesystem::path("data/debug/dump") /
        (std::to_string(timestamp) + "-" +
         sanitizeAttachmentName(attachmentName) + ".bmp"));
    (void)requestedScreenPath;

    auto readback = VulkanBuffer::create(
        device(), byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    device().waitIdle();
    const VkImageLayout previousLayout = attachment.currentLayout;
    const auto attachmentKind =
        (attachment.aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0
            ? LX_core::FrameGraphAttachmentKind::Color
            : LX_core::FrameGraphAttachmentKind::Depth;
    auto cmd = commandBufferManager().beginSingleTimeCommands();
    transitionFrameGraphAttachment(
        LX_core::FrameGraphResourceRef{attachmentId, attachmentKind},
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, *cmd);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = attachment.aspect;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(), attachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);

    transitionFrameGraphAttachment(
        LX_core::FrameGraphResourceRef{attachmentId, attachmentKind},
        previousLayout, dumpRestoreStage(previousLayout, attachment.aspect),
        dumpRestoreAccess(previousLayout, attachment.aspect), *cmd);
    commandBufferManager().endSingleTimeCommands(std::move(cmd),
                                                 device().getGraphicsQueue());

    const void *mapped = readback->map();
    const DumpScalarStats stats =
        computeDumpScalarStats(attachment.format, width, height, mapped);
    std::vector<unsigned char> bgrPixels =
        makeBmpPixelsFromDump(attachment.format, width, height, mapped);
    readback->unmap();

    writeBmp24File(path, width, height, bgrPixels);

    return VulkanFrameGraphAttachmentDumpResult{
        .path = path,
        .screenPath = {},
        .width = width,
        .height = height,
        .format = vkFormatName(attachment.format),
        .minValue = stats.minValue,
        .maxValue = stats.maxValue,
        .meanValue = stats.meanValue,
        .nonZeroRatio = stats.nonZeroRatio,
    };
  }

  VulkanFrameGraphAttachmentDumpResult
  statsFrameGraphAttachment(std::string_view attachmentName) {
    if (!m_foundation) {
      throw std::runtime_error("renderer is not initialized");
    }

    const StringID attachmentId{std::string(attachmentName)};
    auto attachmentOpt =
        resourceManager().getFrameGraphAttachment(attachmentId);
    if (!attachmentOpt.has_value()) {
      throw std::runtime_error("frame graph attachment not available: " +
                               std::string(attachmentName));
    }
    auto &attachment = attachmentOpt->get();

    const u32 width = attachment.extent.width;
    const u32 height = attachment.extent.height;
    const VkDeviceSize byteSize =
        dumpByteSize(attachment.format, width, height);
    if (width == 0 || height == 0 || byteSize == 0) {
      throw std::runtime_error("frame graph attachment has empty extent: " +
                               std::string(attachmentName));
    }

    auto readback = VulkanBuffer::create(
        device(), byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    device().waitIdle();
    const VkImageLayout previousLayout = attachment.currentLayout;
    const auto attachmentKind =
        (attachment.aspect & VK_IMAGE_ASPECT_COLOR_BIT) != 0
            ? LX_core::FrameGraphAttachmentKind::Color
            : LX_core::FrameGraphAttachmentKind::Depth;
    auto cmd = commandBufferManager().beginSingleTimeCommands();
    transitionFrameGraphAttachment(
        LX_core::FrameGraphResourceRef{attachmentId, attachmentKind},
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_READ_BIT, *cmd);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = attachment.aspect;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(), attachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);

    transitionFrameGraphAttachment(
        LX_core::FrameGraphResourceRef{attachmentId, attachmentKind},
        previousLayout, dumpRestoreStage(previousLayout, attachment.aspect),
        dumpRestoreAccess(previousLayout, attachment.aspect), *cmd);
    commandBufferManager().endSingleTimeCommands(std::move(cmd),
                                                 device().getGraphicsQueue());

    const void *mapped = readback->map();
    const DumpScalarStats stats =
        computeDumpScalarStats(attachment.format, width, height, mapped);
    readback->unmap();

    return VulkanFrameGraphAttachmentDumpResult{
        .path = {},
        .screenPath = {},
        .width = width,
        .height = height,
        .format = vkFormatName(attachment.format),
        .minValue = stats.minValue,
        .maxValue = stats.maxValue,
        .meanValue = stats.meanValue,
        .nonZeroRatio = stats.nonZeroRatio,
    };
  }

  VulkanFrameGraphAttachmentDumpResult dumpDebugRenderTarget(
      std::string_view passName, const std::optional<std::string> &cameraPath,
      const std::optional<std::filesystem::path> &requestedPath) {
    if (!m_foundation || !m_swapchain || !m_scene) {
      throw std::runtime_error("renderer is not initialized");
    }

    const StringID pass = passIdFromDebugName(passName);
    auto camera = cameraForDebugDump(cameraPath);
    if (!camera.has_value()) {
      throw std::runtime_error("debug render target camera not found");
    }

    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const std::filesystem::path path =
        requestedPath.value_or(std::filesystem::path("data/debug/dump") /
                               (std::to_string(timestamp) + "-" +
                                sanitizeAttachmentName(passName) + ".bmp"));

    LX_core::RenderTargetDesc targetDesc;
    targetDesc.role = LX_core::RenderTargetRole::Offscreen;
    targetDesc.colorFormat = LX_core::ImageFormat::BGRA8;
    targetDesc.depthFormat = LX_core::ImageFormat::D32Float;
    const LX_core::RenderTarget target{targetDesc};

    auto &cameraComponent = camera->get();
    updateDirectionalLightCascadesForCamera(cameraComponent);
    LX_core::CameraResource cameraResource =
        LX_core::Scene::makeCameraResource(cameraComponent.getSnapshot());

    LX_core::RenderWorkQueue queue;
    queue.build(LX_core::RenderWorkBuildContext::realtime(
                    *m_scene,
                    LX_core::RenderWorkBuildContext::RealtimeOptions{
                        .cameraResource = cameraResource,
                        .visibleMask = cameraComponent.getCullingMask() &
                                       ~LX_core::Layer_EditorOverlay,
                    }),
                pass, target);
    if (queue.getItems().empty()) {
      throw std::runtime_error("debug render target produced no draw items");
    }

    syncRenderUploadPlan(queue);
    resourceManager().preloadPipelines(queue.collectUniquePipelineBuildDescs());

    const VkExtent2D extent = m_swapchain->getExtent();
    const auto colorRef = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("debug.dump.color." + std::to_string(timestamp)));
    const auto depthRef = LX_core::FrameGraphResourceRef::depthAttachment(
        LX_core::StringID("debug.dump.depth." + std::to_string(timestamp)));

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent.width) *
                                  static_cast<VkDeviceSize>(extent.height) * 4u;
    auto readback = VulkanBuffer::create(
        device(), byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    device().waitIdle();
    auto cmd = commandBufferManager().beginSingleTimeCommands();

    auto &colorAttachment = resourceManager().createOrGetFrameGraphAttachment(
        colorRef.name, extent, VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT);
    auto &depthAttachment = resourceManager().createOrGetFrameGraphAttachment(
        depthRef.name, extent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    transitionFrameGraphAttachment(
        colorRef, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, *cmd);
    transitionFrameGraphAttachment(
        depthRef, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, *cmd);

    std::vector<VkImageView> attachments{
        colorAttachment.texture->getImageView(),
        depthAttachment.texture->getImageView(),
    };
    auto &renderPass = resourceManager().getRenderPass(targetDesc);
    auto framebuffer = VulkanFrameBuffer::create(
        device(), renderPass.getHandle(), attachments, extent);

    cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                         extent, renderPass.getClearValues());
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    for (auto &item : queue.getItems()) {
      auto pipeline = resourceManager().getOrCreatePipeline(item);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(resourceManager(), pipeline, item);
      cmd->executeWorkItem(item);
    }
    cmd->endRenderPass();

    transitionFrameGraphAttachment(
        colorRef, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, *cmd);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(),
                           colorAttachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);
    commandBufferManager().endSingleTimeCommands(std::move(cmd),
                                                 device().getGraphicsQueue());

    const auto *rgba = static_cast<const unsigned char *>(readback->map());
    std::vector<unsigned char> bgrPixels;
    bgrPixels.reserve(static_cast<usize>(extent.width) *
                      static_cast<usize>(extent.height) * 3u);
    for (u32 y = 0; y < extent.height; ++y) {
      for (u32 x = 0; x < extent.width; ++x) {
        const usize i =
            (static_cast<usize>(y) * extent.width + static_cast<usize>(x)) * 4u;
        bgrPixels.push_back(rgba[i + 0u]);
        bgrPixels.push_back(rgba[i + 1u]);
        bgrPixels.push_back(rgba[i + 2u]);
      }
    }
    readback->unmap();

    writeBmp24File(path, extent.width, extent.height, bgrPixels);
    return VulkanFrameGraphAttachmentDumpResult{
        .path = path,
        .screenPath = {},
        .width = extent.width,
        .height = extent.height,
        .format = vkFormatName(VK_FORMAT_B8G8R8A8_UNORM),
    };
  }

  VulkanRealtimeProfileOutputResult
  generateRealtimeProfileOutput(SceneSharedPtr scene,
                                const LX_core::offline::OutputProfile &output,
                                const std::filesystem::path &basePath) {
    if (!m_foundation || !m_swapchain) {
      throw std::runtime_error("renderer is not initialized");
    }
    if (!scene) {
      throw std::runtime_error("realtime profile output requires a scene");
    }
    if (output.width == 0 || output.height == 0) {
      throw std::runtime_error(
          "realtime profile output extent must be positive");
    }
    const SceneSharedPtr previousScene = m_scene;
    m_scene = std::move(scene);
    struct RendererSceneRestore final {
      Impl &renderer;
      SceneSharedPtr scene;
      ~RendererSceneRestore() {
        renderer.m_scene = std::move(scene);
        renderer.updateDirectionalLightCascades();
      }
    } sceneRestore{.renderer = *this, .scene = previousScene};

    LX_core::SceneNode *cameraNode = m_scene->findByPath(output.cameraPath);
    if (!cameraNode) {
      throw std::runtime_error("realtime profile camera not found: " +
                               output.cameraPath);
    }
    auto cameraOpt = cameraNode->getComponent<LX_core::CameraComponent>();
    if (!cameraOpt.has_value()) {
      throw std::runtime_error("realtime profile path is not a camera: " +
                               output.cameraPath);
    }
    const auto &camera = cameraOpt->get();

    const float profileAspect =
        static_cast<float>(output.width) / static_cast<float>(output.height);
    const LX_core::CameraSnapshot sourceCamera =
        camera.getSnapshot(output.cameraPath);
    const LX_core::CameraProjection outputProjection =
        LX_core::offline::resolveOutputCameraProjection(sourceCamera.projection,
                                                        output);
    const LX_core::VisibilityLayerMask outputCullingMask =
        output.cameraOverrides.cullingMask.value_or(sourceCamera.cullingMask);

    LX_core::CameraResource outputCameraResource;
    outputCameraResource.pose = sourceCamera.pose;
    outputCameraResource.projection = outputProjection;
    outputCameraResource.view = LX_core::makeCameraViewMatrix(sourceCamera.pose);
    outputCameraResource.proj =
        LX_core::makeCameraProjectionMatrix(outputProjection);
    outputCameraResource.cullingMask = outputCullingMask;
    outputCameraResource.active = sourceCamera.active;
    const auto outputCameraView = outputCameraResource.view;
    const auto outputCameraProj = outputCameraResource.proj;

    LX_core::RenderTargetDesc targetDesc;
    targetDesc.role = LX_core::RenderTargetRole::Offscreen;
    targetDesc.colorFormat = LX_core::ImageFormat::RGBA16Float;
    targetDesc.depthFormat = LX_core::ImageFormat::D32Float;
    const LX_core::RenderTarget target{targetDesc};

    auto sceneResources = m_scene->getSceneLevelResources(
        LX_core::Pass_Forward, outputCameraResource);
    RealtimeProfileDebugInfo debugInfo;
    debugInfo.profileAspect = profileAspect;
    debugInfo.cameraAspect = outputProjection.aspect;
    debugInfo.cameraView = outputCameraView;
    debugInfo.cameraProj = outputCameraProj;
    debugInfo.cameraResourceCount = countCameraResources(sceneResources);
    debugInfo.lightResourceCount = countLightResources(sceneResources);
    debugInfo.lightDirection = findLightDirection(sceneResources);

    const std::string attachmentPrefix =
        "realtime.profile." + basePath.generic_string() + "." +
        std::to_string(output.width) + "x" + std::to_string(output.height);

    LX_core::RenderWorkQueue queue;
    queue.build(LX_core::RenderWorkBuildContext::realtime(
                    *m_scene,
                    LX_core::RenderWorkBuildContext::RealtimeOptions{
                        .cameraResource = outputCameraResource,
                        .visibleMask =
                            outputCullingMask & ~LX_core::Layer_EditorOverlay,
                    }),
                LX_core::Pass_Forward, target);
    if (queue.getItems().empty()) {
      throw std::runtime_error(
          "realtime profile output produced no draw items");
    }
    debugInfo.drawItemCount = static_cast<u32>(queue.getItems().size());
    const LX_core::Mat4f viewProj = outputCameraProj * outputCameraView;
    for (const auto &renderable : m_scene->getRenderables()) {
      if (!renderable) {
        continue;
      }
      const LX_core::StringID debugId = renderable->getDebugId();
      for (const auto &item : queue.getItems()) {
        if (item.debugId != debugId) {
          continue;
        }
        debugInfo.projectedBounds.push_back(makeProjectedBoundsDebug(
            *renderable, item, viewProj, output.width, output.height));
        break;
      }
    }
    syncRenderUploadPlan(queue);
    resourceManager().preloadPipelines(queue.collectUniquePipelineBuildDescs());

    const VkExtent2D extent{output.width, output.height};
    const auto colorRef = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID(attachmentPrefix + ".color"));
    const auto depthRef = LX_core::FrameGraphResourceRef::depthAttachment(
        LX_core::StringID(attachmentPrefix + ".depth"));
    const VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    const VkDeviceSize byteSize =
        dumpByteSize(colorFormat, output.width, output.height);
    auto readback = VulkanBuffer::create(
        device(), byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const VkDeviceSize depthByteSize =
        dumpByteSize(VK_FORMAT_D32_SFLOAT, output.width, output.height);
    auto depthReadback = VulkanBuffer::create(
        device(), depthByteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    device().waitIdle();
    auto cmd = commandBufferManager().beginSingleTimeCommands();
    auto &colorAttachment = resourceManager().createOrGetFrameGraphAttachment(
        colorRef.name, extent, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT);
    auto &depthAttachment = resourceManager().createOrGetFrameGraphAttachment(
        depthRef.name, extent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    transitionFrameGraphAttachment(
        colorRef, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, *cmd);
    transitionFrameGraphAttachment(
        depthRef, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, *cmd);

    std::vector<VkImageView> attachments{
        colorAttachment.texture->getImageView(),
        depthAttachment.texture->getImageView(),
    };
    auto &renderPass = resourceManager().getRenderPass(targetDesc);
    auto framebuffer = VulkanFrameBuffer::create(
        device(), renderPass.getHandle(), attachments, extent);
    debugInfo.viewportExtent = extent;
    auto clearValues = renderPass.getClearValues();
    if (!clearValues.empty()) {
      clearValues[0].color = {output.backgroundColor.x,
                              output.backgroundColor.y,
                              output.backgroundColor.z, 1.0f};
    }
    cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                         extent, clearValues);
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    for (auto &item : queue.getItems()) {
      auto pipeline = resourceManager().getOrCreatePipeline(item);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(resourceManager(), pipeline, item);
      cmd->executeWorkItem(item);
    }
    cmd->endRenderPass();

    transitionFrameGraphAttachment(
        colorRef, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, *cmd);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(),
                           colorAttachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);
    transitionFrameGraphAttachment(
        depthRef, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, *cmd);
    VkBufferImageCopy depthRegion{};
    depthRegion.bufferOffset = 0;
    depthRegion.bufferRowLength = 0;
    depthRegion.bufferImageHeight = 0;
    depthRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthRegion.imageSubresource.mipLevel = 0;
    depthRegion.imageSubresource.baseArrayLayer = 0;
    depthRegion.imageSubresource.layerCount = 1;
    depthRegion.imageOffset = {0, 0, 0};
    depthRegion.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd->getHandle(),
                           depthAttachment.texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           depthReadback->getHandle(), 1, &depthRegion);
    commandBufferManager().endSingleTimeCommands(std::move(cmd),
                                                 device().getGraphicsQueue());

    const void *mapped = readback->map();
    LX_core::offline::OfflineReadbackImage image = makeRgba32fImageFromDump(
        colorFormat, output.width, output.height, mapped);
    readback->unmap();

    const std::filesystem::path outputDir = basePath.parent_path();
    std::filesystem::create_directories(outputDir);
    const std::string outputStem = basePath.filename().empty()
                                       ? std::string("render")
                                       : basePath.filename().generic_string();
    VulkanRealtimeProfileOutputResult result{
        .linearExrPath = outputDir / (outputStem + "-linear.exr"),
        .cpuSrgbPngPath = outputDir / (outputStem + "-cpu_srgb.png"),
        .pipelineSrgbPngPath = {},
        .depthDebugPath = outputDir / (outputStem + "-depth.bmp"),
        .metadataPath = outputDir / (outputStem + ".json"),
        .width = output.width,
        .height = output.height,
    };
    LX_infra::image::writeRgba32fExr(result.linearExrPath, image);
    LX_infra::image::writeToneMappedPng(
        result.cpuSrgbPngPath, image,
        LX_core::image::ToneMappingSettings{
            .exposure = 1.0f,
            .gamma = 2.2f,
            .mode = LX_core::image::ToneMappingMode::Aces,
        });
    const void *mappedDepth = depthReadback->map();
    std::vector<unsigned char> depthBgrPixels = makeBmpPixelsFromDump(
        VK_FORMAT_D32_SFLOAT, output.width, output.height, mappedDepth);
    depthReadback->unmap();
    writeBmp24File(result.depthDebugPath, output.width, output.height,
                   depthBgrPixels);
    writeRealtimeProfileMetadata(result.metadataPath, result,
                                 "unavailable: pipeline sRGB readback is not "
                                 "implemented in this path",
                                 debugInfo);
    return result;
  }

private:
  struct PendingScreenDump final {
    std::filesystem::path path;
    VulkanBufferUniquePtr readback;
    u32 width = 0;
    u32 height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
  };

  usize findFinalSwapchainPassIndex() const {
    const auto &passes = m_compiledFrameGraph.getPasses();
    for (usize i = passes.size(); i > 0; --i) {
      if (passes[i - 1].target.role == LX_core::RenderTargetRole::Swapchain) {
        return i - 1;
      }
    }
    return passes.size();
  }

  usize findFinalSwapchainGroupStartIndex(usize finalSwapchainPassIndex) const {
    const auto &passes = m_compiledFrameGraph.getPasses();
    if (finalSwapchainPassIndex >= passes.size()) {
      return passes.size();
    }

    usize start = finalSwapchainPassIndex;
    while (start > 0 && passes[start - 1].target.role ==
                            LX_core::RenderTargetRole::Swapchain) {
      --start;
    }

    // Legacy VkRenderPass cannot preserve forward color for debug/GUI overlay
    // if we end and begin again with the current clear/load contract. Only the
    // final contiguous swapchain run is intentionally grouped under one active
    // VkRenderPass; offscreen passes and non-final swapchain passes remain
    // one begin/end per compiled pass.
    return start;
  }

  void drawPassQueue(usize passIndex, VulkanCommandBuffer &cmd) {
    if (passIndex >= m_compiledFrameGraph.getPasses().size()) {
      return;
    }
    const usize sourcePassIndex =
        m_compiledFrameGraph.getPasses()[passIndex].sourcePassIndex;
    if (sourcePassIndex >= m_frameGraph.getPasses().size()) {
      return;
    }

    auto &pass = m_frameGraph.getPasses()[sourcePassIndex];
    auto &queue = pass.queue;
    auto &items = queue.getItems();
    const bool strictBindlessValidation = strictBindlessValidationEnabled();
    const bool migratedValidationPass =
        isMigratedBindlessValidationPass(pass.name);
    const auto decision = LX_core::decideBindlessSubmission(
        queue, pass.name, strictBindlessValidation, migratedValidationPass);
    if (decision.kind == LX_core::BindlessSubmissionDecisionKind::
                             StrictValidationRejected) {
      throw std::runtime_error(
          "bindless validation rejected migrated pass: " +
          decision.validation.diagnostics.front().reason);
    }

    if (decision.kind == LX_core::BindlessSubmissionDecisionKind::BindlessBatch) {
      const auto batches = queue.compileIndirectBatches();
      for (const auto &batch : batches) {
        if (batch.sourceItemIndices.empty()) {
          continue;
        }
        LX_core::RenderWorkItem batchItem = items[batch.sourceItemIndices[0]];
        batchItem.kind = LX_core::RenderWorkKind::RasterBatch;
        batchItem.descriptorResources = batch.descriptorResources;
        batchItem.raster.vertexBuffer = batch.vertexBuffer;
        batchItem.raster.indexBuffer = batch.indexBuffer;
        batchItem.rasterBatch.commands = batch.commands;
        batchItem.rasterBatch.sourceItemIndices = batch.sourceItemIndices;
        auto pipeline = resourceManager().getOrCreatePipeline(batchItem);
        cmd.bindPipeline(pipeline);
        cmd.bindResources(resourceManager(), pipeline, batchItem);
        cmd.executeWorkItem(batchItem);
      }
      return;
    }

    for (auto &item : items) {
      auto pipeline = resourceManager().getOrCreatePipeline(item);
      cmd.bindPipeline(pipeline);
      cmd.bindResources(resourceManager(), pipeline, item);
      cmd.executeWorkItem(item);
    }
  }

  void addFullscreenMaterialItem(LX_core::StringID pass,
                                 const LX_core::RenderTargetDesc &target,
                                 LX_core::MaterialInstance::UniquePtr material,
                                 const char *objectSignature) {
    if (!m_scene || !material) {
      return;
    }
    LX_core::RenderWorkItem item;
    item.shaderInfo = material->getPassShader(pass);
    item.renderState = material->getPassRenderState(pass);
    const LX_core::StringID materialSignature =
        material->getPipelineSignature(pass);
    auto vertexBuffer = LX_core::VertexBuffer<LX_core::VertexPos>::createUnique(
        std::vector<LX_core::VertexPos>{{{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}}});
    auto indexBuffer = LX_core::IndexBuffer::createUnique({0u, 1u, 2u});
    item.raster.vertexBuffer =
        m_scene->resources().addRenderGpuResource(std::move(vertexBuffer));
    item.raster.indexBuffer =
        m_scene->resources().addRenderGpuResource(std::move(indexBuffer));
    const LX_core::MaterialHandle materialHandle =
        m_scene->resources().addRenderMaterial(std::move(material));
    item.descriptorResources =
        LX_core::buildSceneMaterialDescriptorResources(
            m_scene->resources(), materialHandle, item.shaderInfo);
    item.pass = pass;
    item.target = target;
    item.objectSignature = LX_core::StringID(objectSignature);
    item.materialSignature = materialSignature;
    item.pipelineKey = LX_core::PipelineKey::build(
        item.objectSignature, item.materialSignature,
        item.target.getPipelineSignature());

    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == item.pass) {
        pass.queue.addItem(std::move(item));
        pass.queue.sort();
        return;
      }
    }
  }

  void addBloomThresholdItem() {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    addFullscreenMaterialItem(LX_core::Pass_BloomThreshold,
                              LX_core::RenderTargetDesc::offscreenColor(
                                  LX_core::ImageFormat::RGBA16Float),
                              builder.createBloomThresholdMaterial(),
                              "BloomThresholdFullscreenTriangle");
  }

  void addBloomBlurItem(LX_core::StringID pass, const char *shaderName,
                        const char *objectSignature) {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    addFullscreenMaterialItem(pass,
                              LX_core::RenderTargetDesc::offscreenColor(
                                  LX_core::ImageFormat::RGBA16Float),
                              builder.createBloomBlurMaterial(pass, shaderName),
                              objectSignature);
  }

  void addStandardPostProcessItem(const LX_core::RenderTargetDesc &target) {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    addFullscreenMaterialItem(LX_core::Pass_PostProcess, target,
                              builder.createStandardPostProcessMaterial(),
                              "PostProcessFullscreenTriangle");
  }

  void addDeferredLightingItem(const LX_core::RenderTargetDesc &target) {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    auto material = builder.createDeferredLightingMaterial();
    if (!m_scene || !material) {
      return;
    }

    LX_core::RenderWorkItem item;
    item.shaderInfo = material->getPassShader(LX_core::Pass_DeferredLighting);
    item.renderState =
        material->getPassRenderState(LX_core::Pass_DeferredLighting);
    const LX_core::StringID materialSignature =
        material->getPipelineSignature(LX_core::Pass_DeferredLighting);
    auto vertexBuffer = LX_core::VertexBuffer<LX_core::VertexPos>::createUnique(
        std::vector<LX_core::VertexPos>{{{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}}});
    auto indexBuffer = LX_core::IndexBuffer::createUnique({0u, 1u, 2u});
    item.raster.vertexBuffer =
        m_scene->resources().addRenderGpuResource(std::move(vertexBuffer));
    item.raster.indexBuffer =
        m_scene->resources().addRenderGpuResource(std::move(indexBuffer));
    const LX_core::MaterialHandle materialHandle =
        m_scene->resources().addRenderMaterial(std::move(material));
    item.descriptorResources =
        LX_core::buildSceneMaterialDescriptorResources(
            m_scene->resources(), materialHandle, item.shaderInfo);

    const LX_core::RenderTarget defaultCameraTarget{};
    auto sceneResources = m_scene->getSceneLevelResources(
        LX_core::Pass_DeferredLighting, defaultCameraTarget);
    for (auto &resource : sceneResources) {
      item.descriptorResources.emplace_back(std::move(resource));
    }

    item.pass = LX_core::Pass_DeferredLighting;
    item.target = target;
    item.objectSignature =
        LX_core::StringID("DeferredLightingFullscreenTriangle");
    item.materialSignature = materialSignature;
    item.pipelineKey = LX_core::PipelineKey::build(
        item.objectSignature, item.materialSignature,
        item.target.getPipelineSignature());

    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == LX_core::Pass_DeferredLighting) {
        pass.queue.addItem(std::move(item));
        pass.queue.sort();
        return;
      }
    }
  }

  void addSkyboxBackgroundItem(const LX_core::RenderTargetDesc &target) {
    if (!m_scene) {
      return;
    }
    const auto *iblResources =
        m_scene->resources().getIblEnvironmentResourceSet();
    if (iblResources == nullptr || !iblResources->skyboxEnabled ||
        !iblResources->environmentUbo ||
        iblResources->environmentUbo->getIblIntensity() <= 0.0f) {
      return;
    }
    LX_core::GpuResourceRef skyboxResource;
    if (iblResources->bakedSkyboxCubemap) {
      skyboxResource =
          LX_core::GpuResourceRef{*iblResources->bakedSkyboxCubemap};
    } else if (iblResources->skyboxCubemap) {
      skyboxResource = LX_core::GpuResourceRef{*iblResources->skyboxCubemap};
    }
    if (!skyboxResource.isValid()) {
      return;
    }

    VulkanPostProcessBuilder builder(m_postProcessSettings);
    auto material = builder.createSkyboxBackgroundMaterial();
    LX_core::RenderWorkItem item;
    item.shaderInfo = material->getPassShader(LX_core::Pass_Forward);
    item.renderState = material->getPassRenderState(LX_core::Pass_Forward);
    const LX_core::StringID materialSignature =
        material->getPipelineSignature(LX_core::Pass_Forward);
    auto vertexBuffer = LX_core::VertexBuffer<LX_core::VertexPos>::createUnique(
        std::vector<LX_core::VertexPos>{{{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}}});
    auto indexBuffer = LX_core::IndexBuffer::createUnique({0u, 1u, 2u});
    item.raster.vertexBuffer =
        m_scene->resources().addRenderGpuResource(std::move(vertexBuffer));
    item.raster.indexBuffer =
        m_scene->resources().addRenderGpuResource(std::move(indexBuffer));
    const LX_core::MaterialHandle materialHandle =
        m_scene->resources().addRenderMaterial(std::move(material));
    item.descriptorResources =
        LX_core::buildSceneMaterialDescriptorResources(
            m_scene->resources(), materialHandle, item.shaderInfo);
    const LX_core::RenderTarget renderTarget{};
    auto sceneResources =
        m_scene->getSceneLevelResources(LX_core::Pass_Forward, renderTarget);
    for (auto &resource : sceneResources) {
      item.descriptorResources.emplace_back(std::move(resource));
    }
    item.descriptorResources.emplace_back(skyboxResource.get());
    item.descriptorResources.emplace_back(*iblResources->environmentUbo);
    item.pass = LX_core::Pass_Forward;
    item.target = target;
    item.objectSignature = LX_core::StringID("SkyboxFullscreenTriangle");
    item.materialSignature = materialSignature;
    item.pipelineKey = LX_core::PipelineKey::build(
        item.objectSignature, item.materialSignature,
        item.target.getPipelineSignature());

    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == LX_core::Pass_Forward) {
        pass.queue.addItem(std::move(item));
        pass.queue.sort();
        return;
      }
    }
  }

  void rebuildPassQueueWithDefaultCameraResources(
      LX_core::StringID passName, const LX_core::RenderTargetDesc &targetDesc) {
    if (!m_scene) {
      return;
    }
    const LX_core::RenderTarget defaultCameraTarget{};
    const LX_core::RenderTarget renderTarget{targetDesc};
    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == passName) {
        pass.queue.build(LX_core::RenderWorkBuildContext::realtime(
                             *m_scene,
                             LX_core::RenderWorkBuildContext::RealtimeOptions{
                                 .sceneResourceTarget = defaultCameraTarget,
                             }),
                         passName, renderTarget);
        return;
      }
    }
  }

  void rebuildDebugOverlayQueueWithDefaultCameraResources(
      const LX_core::RenderTargetDesc &debugTarget) {
    if (!m_scene) {
      return;
    }
    const LX_core::RenderTarget defaultCameraTarget{};
    const LX_core::RenderTarget debugRenderTarget{debugTarget};
    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == LX_core::Pass_DebugOverlay) {
        pass.queue.build(LX_core::RenderWorkBuildContext::realtime(
                             *m_scene,
                             LX_core::RenderWorkBuildContext::RealtimeOptions{
                                 .sceneResourceTarget = defaultCameraTarget,
                             }),
                         LX_core::Pass_DebugOverlay, debugRenderTarget);
        return;
      }
    }
  }

  LX_core::DirectionalLight *mainDirectionalLight() const {
    if (!m_scene) {
      return nullptr;
    }
    for (const auto &light : m_scene->getLights()) {
      auto *directional =
          dynamic_cast<LX_core::DirectionalLight *>(&light.get());
      if (directional) {
        if (directional->supportsPass(LX_core::Pass_Shadow) &&
            directional->getSceneNode()) {
          return directional;
        }
      }
    }
    return nullptr;
  }

  std::optional<std::reference_wrapper<LX_core::CameraComponent>>
  mainCameraComponent() const {
    if (!m_scene) {
      return std::nullopt;
    }
    if (const auto cameraNode = m_scene->getActiveCamera()) {
      auto camera = cameraNode->getComponent<LX_core::CameraComponent>();
      if (camera.has_value()) {
        return camera->get();
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<LX_core::CameraComponent>>
  cameraForDebugDump(const std::optional<std::string> &cameraPath) const {
    if (!m_scene) {
      return std::nullopt;
    }
    if (cameraPath.has_value() && !cameraPath->empty()) {
      LX_core::SceneNode *node = m_scene->findByPath(*cameraPath);
      if (!node) {
        return std::nullopt;
      }
      auto camera = node->getComponent<LX_core::CameraComponent>();
      if (!camera) {
        return std::nullopt;
      }
      return camera->get();
    }
    return mainCameraComponent();
  }

  void updateDirectionalLightCascades() {
    auto camera = mainCameraComponent();
    if (!camera.has_value()) {
      return;
    }
    updateDirectionalLightCascadesForCamera(camera->get());
  }

  void updateDirectionalLightCascadesForCamera(
      const LX_core::CameraComponent &camera) {
    const auto light = mainDirectionalLight();
    if (!light) {
      return;
    }
    light->updateShadowCascadesForCamera(camera);
    refreshShadowCascadeUboSnapshots(*light);
  }

  void prepareShadowCascadePass(usize passIndex) {
    if (passIndex >= m_compiledFrameGraph.getPasses().size()) {
      return;
    }
    const auto &pass = m_compiledFrameGraph.getPasses()[passIndex];
    if (pass.name != LX_core::Pass_Shadow) {
      return;
    }
    const auto light = mainDirectionalLight();
    if (!light) {
      return;
    }
    u32 cascadeIndex = 0;
    for (usize i = 0; i < passIndex; ++i) {
      if (m_compiledFrameGraph.getPasses()[i].name == LX_core::Pass_Shadow) {
        ++cascadeIndex;
      }
    }
    if (cascadeIndex < m_shadowCascadeUboSnapshots.size() &&
        m_shadowCascadeUboSnapshots[cascadeIndex]) {
      resourceManager().syncResource(commandBufferManager(),
                                     *m_shadowCascadeUboSnapshots[cascadeIndex]);
    }
  }

  void rebuildShadowCascadeUboSnapshots() {
    m_shadowCascadeUboSnapshots.clear();
    const auto light = mainDirectionalLight();
    if (!light) {
      return;
    }
    m_shadowCascadeUboSnapshots.reserve(LX_core::MaxShadowCascades);
    for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
         ++cascadeIndex) {
      m_shadowCascadeUboSnapshots.push_back(
          light->makeShadowCascadeUBOSnapshot(cascadeIndex));
    }
  }

  void
  refreshShadowCascadeUboSnapshots(const LX_core::DirectionalLight &light) {
    if (m_shadowCascadeUboSnapshots.empty()) {
      return;
    }
    for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
         ++cascadeIndex) {
      if (cascadeIndex >= m_shadowCascadeUboSnapshots.size() ||
          !m_shadowCascadeUboSnapshots[cascadeIndex]) {
        continue;
      }
      m_shadowCascadeUboSnapshots[cascadeIndex]->param =
          light.getDirectionalUBO().param;
      m_shadowCascadeUboSnapshots[cascadeIndex]->param.shadowViewProj =
          light.getDirectionalUBO().param.cascadeViewProj[cascadeIndex];
      m_shadowCascadeUboSnapshots[cascadeIndex]->setDirty();
    }
  }

  void bindShadowCascadeUboSnapshots() {
    const auto light = mainDirectionalLight();
    if (!light || m_shadowCascadeUboSnapshots.empty()) {
      return;
    }

    const auto mainLight = light->getUBO();
    if (!mainLight.isValid()) {
      return;
    }
    const auto mainLightIdentity = mainLight.getBackendCacheIdentity();
    u32 cascadeIndex = 0;
    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name != LX_core::Pass_Shadow) {
        continue;
      }
      if (cascadeIndex >= m_shadowCascadeUboSnapshots.size()) {
        break;
      }
      const auto &snapshot = m_shadowCascadeUboSnapshots[cascadeIndex];
      if (!snapshot) {
        ++cascadeIndex;
        continue;
      }
      for (auto &item : pass.queue.getItems()) {
        for (auto &resource : item.descriptorResources) {
          if (resource.isResource() && resource.resource().isValid() &&
              resource.resource().getBackendCacheIdentity() ==
                  mainLightIdentity) {
            resource = LX_core::DescriptorResourceRef{*snapshot};
          }
        }
      }
      ++cascadeIndex;
    }
  }

  void attachFrameGraphSampledResources() {
    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    auto &graphPasses = m_frameGraph.getPasses();
    for (const auto &compiledPass : compiledPasses) {
      if (compiledPass.sourcePassIndex >= graphPasses.size()) {
        continue;
      }
      for (const auto &read : compiledPass.reads) {
        if (read.bindingName == LX_core::StringID{}) {
          continue;
        }
        auto resource = std::make_unique<LX_core::FrameGraphSampledResource>(
            read.resource, read.bindingName);
        const auto resourceRef =
            m_scene->resources().addRenderGpuResource(std::move(resource));
        for (auto &item :
             graphPasses[compiledPass.sourcePassIndex].queue.getItems()) {
          item.descriptorResources.emplace_back(resourceRef.get());
        }
      }
    }
  }

  void resetOffscreenFramebuffers() {
    m_offscreenFramebuffers.clear();
    m_offscreenFramebuffers.resize(m_compiledFrameGraph.getPasses().size());
    for (auto &passFramebuffers : m_offscreenFramebuffers) {
      passFramebuffers.resize(kMaxFramesInFlight);
    }
  }

  void consumeAcquireSemaphoreAndSignalFenceAfterFailedSubmit(
      VkSemaphore imageAvailableSemaphore, VkPipelineStageFlags waitStage,
      VkFence fence) {
    // The per-frame fence is reset immediately before queue submission. If the
    // real submit fails, the next acquire would block forever on that
    // unsignaled fence. The acquired image semaphore is already signaled, so
    // the recovery submit must wait on it before this frame slot can be reused.
    VkSubmitInfo recoverySubmit{};
    recoverySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    recoverySubmit.waitSemaphoreCount = 1;
    recoverySubmit.pWaitSemaphores = &imageAvailableSemaphore;
    recoverySubmit.pWaitDstStageMask = &waitStage;
    const VkResult recoveryResult =
        vkQueueSubmit(device().getGraphicsQueue(), 1, &recoverySubmit, fence);
    if (recoveryResult != VK_SUCCESS) {
      std::cerr << "[VulkanRenderer] failed to consume acquired semaphore and "
                   "re-signal in-flight fence after submit failure; VkResult="
                << static_cast<int>(recoveryResult) << std::endl;
      m_swapchainNeedsRebuild = true;
    }
  }

  void transitionFrameGraphAttachment(
      const LX_core::FrameGraphResourceRef &resource, VkImageLayout newLayout,
      VkPipelineStageFlags dstStage, VkAccessFlags dstAccess,
      VulkanCommandBuffer &cmd) {
    auto attachmentOpt =
        resourceManager().getFrameGraphAttachment(resource.name);
    if (!attachmentOpt.has_value()) {
      throw std::runtime_error(
          "Frame graph attachment missing during layout transition");
    }
    auto &attachment = attachmentOpt->get();
    if (attachment.currentLayout == newLayout) {
      return;
    }

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags srcAccess = 0;
    if (attachment.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      srcAccess = VK_ACCESS_SHADER_READ_BIT;
    } else if (attachment.currentLayout ==
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (attachment.currentLayout ==
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else if (attachment.currentLayout ==
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = attachment.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = attachment.texture->getHandle();
    barrier.subresourceRange.aspectMask = attachment.aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    cmd.pipelineBarrier(srcStage, dstStage, barrier);
    attachment.currentLayout = newLayout;
  }

  VkExtent2D prepareOffscreenPass(usize passIndex, u32 currentFrameIndex,
                                  const LX_core::CompiledFrameGraphPass &pass,
                                  VkExtent2D fallbackExtent,
                                  VulkanCommandBuffer &cmd) {
    if (pass.target.role == LX_core::RenderTargetRole::Swapchain) {
      return fallbackExtent;
    }

    validateOffscreenWritesMatchTarget(pass);

    std::vector<VkImageView> attachments;
    attachments.reserve(pass.target.colorAttachmentCount() +
                        (pass.target.depthFormat.has_value() ? 1u : 0u));
    const auto appendAttachment = [&](const LX_core::FrameGraphWrite &write,
                                      LX_core::ImageFormat format) {
      const auto kind = write.resource.kind;
      const VkImageUsageFlags usage =
          kind == LX_core::FrameGraphAttachmentKind::Color
              ? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
              : (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      auto &attachment = resourceManager().createOrGetFrameGraphAttachment(
          write.resource.name, fallbackExtent, toVkFormat(format),
          attachmentAspect(kind), usage);
      transitionFrameGraphAttachment(
          write.resource, attachmentWriteLayout(kind),
          attachmentWriteStage(kind), attachmentWriteAccess(kind), cmd);
      attachments.push_back(attachment.texture->getImageView());
    };

    const auto colorFormats = pass.target.getColorFormats();
    const auto colorWrites =
        findWritesForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
    for (usize i = 0; i < colorFormats.size(); ++i) {
      appendAttachment(colorWrites[i].get(), colorFormats[i]);
    }
    if (pass.target.depthFormat.has_value()) {
      const auto write =
          findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Depth);
      appendAttachment(write->get(), *pass.target.depthFormat);
    }

    auto &framebuffer = m_offscreenFramebuffers[passIndex][currentFrameIndex];
    if (!framebuffer) {
      auto &renderPass = resourceManager().getRenderPass(pass.target);
      framebuffer = VulkanFrameBuffer::create(device(), renderPass.getHandle(),
                                              attachments, fallbackExtent);
    }
    return fallbackExtent;
  }

  void
  transitionPassWritesToShaderRead(const LX_core::CompiledFrameGraphPass &pass,
                                   VulkanCommandBuffer &cmd) {
    for (const auto &write : pass.writes) {
      if (pass.target.role == LX_core::RenderTargetRole::Swapchain) {
        continue;
      }
      transitionFrameGraphAttachment(write.resource,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     VK_ACCESS_SHADER_READ_BIT, cmd);
    }
  }

  void recordPendingScreenDump(u32 imageIndex, VkExtent2D extent,
                               VulkanCommandBuffer &cmd) {
    if (!m_pendingScreenDump.has_value()) {
      return;
    }
    auto &dump = *m_pendingScreenDump;
    dump.width = extent.width;
    dump.height = extent.height;
    dump.format = m_swapchain->getImageFormat();
    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(extent.width) *
                                  static_cast<VkDeviceSize>(extent.height) * 4u;
    dump.readback = VulkanBuffer::create(
        device(), byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_swapchain->getImage(imageIndex);
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    cmd.pipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, toTransfer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd.getHandle(), m_swapchain->getImage(imageIndex),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dump.readback->getHandle(), 1, &region);

    VkImageMemoryBarrier toPresent = toTransfer;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    cmd.pipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, toPresent);
  }

  void writeCompletedScreenDumpIfNeeded(VkFence fence) {
    if (!m_pendingScreenDump.has_value() || !m_pendingScreenDump->readback) {
      return;
    }

    auto dump = std::move(*m_pendingScreenDump);
    m_pendingScreenDump.reset();
    vkWaitForFences(device().getLogicalDevice(), 1, &fence, VK_TRUE,
                    UINT64_MAX);

    const auto *rgba = static_cast<const unsigned char *>(dump.readback->map());
    std::vector<unsigned char> bgrPixels;
    bgrPixels.reserve(static_cast<usize>(dump.width) *
                      static_cast<usize>(dump.height) * 3u);
    const bool sourceIsBgra = dump.format == VK_FORMAT_B8G8R8A8_UNORM ||
                              dump.format == VK_FORMAT_B8G8R8A8_SRGB;
    for (u32 y = 0; y < dump.height; ++y) {
      for (u32 x = 0; x < dump.width; ++x) {
        const usize i =
            (static_cast<usize>(y) * dump.width + static_cast<usize>(x)) * 4u;
        if (sourceIsBgra) {
          bgrPixels.push_back(rgba[i + 0u]);
          bgrPixels.push_back(rgba[i + 1u]);
          bgrPixels.push_back(rgba[i + 2u]);
        } else {
          bgrPixels.push_back(rgba[i + 2u]);
          bgrPixels.push_back(rgba[i + 1u]);
          bgrPixels.push_back(rgba[i + 0u]);
        }
      }
    }
    dump.readback->unmap();
    writeBmp24File(dump.path, dump.width, dump.height, bgrPixels);
  }

  void rebuildSwapchain() {
    // A zero-sized window (minimized, or mid-drag) produces an invalid
    // swapchain. Let draw() retry later when the window has real size.
    if (m_window && (m_window->getWidth() <= 0 || m_window->getHeight() <= 0)) {
      return;
    }
    m_swapchain->waitIdle();
    if (!m_swapchain->rebuild(resourceManager().getRenderPass())) {
      return;
    }
    resetOffscreenFramebuffers();
    resourceManager().clearFrameGraphAttachments();
    m_swapchainNeedsRebuild = false;
    m_gui.updateSwapchainImageCount(m_swapchain->getImageCount());
  }

  void destroy() {
    if (m_foundation) {
      // 关键：等 GPU 干完活再删东西
      device().waitIdle();
    }
    // REQ-017: tear down ImGui before releasing Vulkan device so that
    // ImGui's descriptor pool / backend objects still see a live VkDevice.
    if (m_gui.isInitialized()) {
      m_gui.shutdown();
    }
    // Offscreen frame-graph framebuffers depend on the Vulkan device and must
    // be released before the device/resource manager are torn down.
    m_offscreenFramebuffers.clear();
    // Swapchain depends on the shared foundation device and render passes.
    m_swapchain.reset();
    m_foundation.reset();
  }

  WindowSharedPtr m_window;
  VulkanRendererFoundationUniquePtr m_foundation = nullptr;
  VulkanSwapchainUniquePtr m_swapchain = nullptr;
  SceneSharedPtr m_scene = nullptr;
  LX_core::FrameGraph m_frameGraph{};
  LX_core::CompiledFrameGraph m_compiledFrameGraph{};
  std::vector<std::vector<std::unique_ptr<VulkanFrameBuffer>>>
      m_offscreenFramebuffers;
  u32 m_frameIndex = 0;
  usize m_initSceneCallCount = 0;
  bool m_swapchainNeedsRebuild = false;
  VulkanPostProcessSettings m_postProcessSettings{};
  infra::Gui m_gui{};
  std::function<void()> m_drawUiCallback{};
  std::optional<PendingScreenDump> m_pendingScreenDump;
  std::vector<LX_core::DirectionalLightDataUniquePtr>
      m_shadowCascadeUboSnapshots;
};

VulkanRealtimeRenderer::VulkanRealtimeRenderer()
    : p_impl(std::make_unique<Impl>()) {}

VulkanRealtimeRenderer::~VulkanRealtimeRenderer() = default;

void VulkanRealtimeRenderer::initialize(WindowSharedPtr window,
                                        const char *appName) {
  p_impl->initialize(std::move(window), appName);
}

void VulkanRealtimeRenderer::shutdown() { p_impl->shutdown(); }

void VulkanRealtimeRenderer::initScene(SceneSharedPtr scene) {
  p_impl->initScene(std::move(scene));
}

void VulkanRealtimeRenderer::uploadData() { p_impl->uploadData(); }

void VulkanRealtimeRenderer::draw() { p_impl->draw(); }

void VulkanRealtimeRenderer::setDrawUiCallback(std::function<void()> cb) {
  p_impl->setDrawUiCallback(std::move(cb));
}

void VulkanRealtimeRenderer::setPostProcessSettings(
    const VulkanPostProcessSettings &settings) {
  p_impl->setPostProcessSettings(settings);
}

const VulkanPostProcessSettings &
VulkanRealtimeRenderer::postProcessSettings() const {
  return p_impl->postProcessSettings();
}

usize VulkanRealtimeRenderer::cachedResourceCount() const {
  return p_impl->cachedResourceCount();
}

usize VulkanRealtimeRenderer::frameGraphItemCount() const {
  return p_impl->frameGraphItemCount();
}

usize VulkanRealtimeRenderer::compiledFrameGraphPassCount() const {
  return p_impl->compiledFrameGraphPassCount();
}

std::vector<std::string>
VulkanRealtimeRenderer::compiledFrameGraphPassNames() const {
  return p_impl->compiledFrameGraphPassNames();
}

usize VulkanRealtimeRenderer::frameGraphAttachmentCount() const {
  return p_impl->frameGraphAttachmentCount();
}

usize VulkanRealtimeRenderer::initSceneCallCount() const {
  return p_impl->initSceneCallCount();
}

VulkanFrameGraphAttachmentDumpResult
VulkanRealtimeRenderer::dumpFrameGraphAttachment(
    std::string_view attachmentName,
    const std::optional<std::filesystem::path> &path,
    const std::optional<std::filesystem::path> &screenPath) {
  return p_impl->dumpFrameGraphAttachment(attachmentName, path, screenPath);
}

VulkanFrameGraphAttachmentDumpResult
VulkanRealtimeRenderer::statsFrameGraphAttachment(
    std::string_view attachmentName) {
  return p_impl->statsFrameGraphAttachment(attachmentName);
}

VulkanFrameGraphAttachmentDumpResult
VulkanRealtimeRenderer::dumpDebugRenderTarget(
    std::string_view passName, const std::optional<std::string> &cameraPath,
    const std::optional<std::filesystem::path> &path) {
  return p_impl->dumpDebugRenderTarget(passName, cameraPath, path);
}

VulkanRealtimeProfileOutputResult
VulkanRealtimeRenderer::generateRealtimeProfileOutput(
    SceneSharedPtr scene, const LX_core::offline::OutputProfile &output,
    const std::filesystem::path &basePath) {
  return p_impl->generateRealtimeProfileOutput(std::move(scene), output,
                                               basePath);
}

} // namespace LX_core::backend
