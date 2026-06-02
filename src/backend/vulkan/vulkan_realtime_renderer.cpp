#include "vulkan_realtime_renderer.hpp"
#include "vulkan_renderer_foundation.hpp"
#include "vulkan_post_process_builder.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"
#include "core/asset/material_template.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_scene.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include "infra/image/rgba_image_io.hpp"
#include "infra/gui/gui.hpp"
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
  const VkDeviceSize pixelCount = static_cast<VkDeviceSize>(width) *
                                  static_cast<VkDeviceSize>(height);
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
  return signScale *
         std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
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

std::vector<unsigned char> makeBmpPixelsFromDump(
    VkFormat format, u32 width, u32 height, const void *mappedData) {
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

LX_core::offline::OfflineReadbackImage makeRgba32fImageFromDump(
    VkFormat format, u32 width, u32 height, const void *mappedData) {
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
  u32 drawItemCount = 0;
  VkExtent2D viewportExtent{};
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

[[nodiscard]] u32 countCameraResources(
    const std::vector<LX_core::IGpuResourceSharedPtr> &resources) {
  u32 count = 0;
  const LX_core::StringID cameraBinding("CameraUBO");
  for (const auto &resource : resources) {
    if (resource && resource->getBindingName() == cameraBinding) {
      ++count;
    }
  }
  return count;
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
    const LX_core::IRenderable &renderable, const LX_core::RenderingItem &item,
    const LX_core::Mat4f &viewProj, u32 width, u32 height) {
  ProjectedBoundsDebug out;
  out.nodeName = renderable.getNodeName();
  out.objectSignature =
      LX_core::GlobalStringTable::get().getName(item.objectSignature.id);
  out.indexCount = item.indexBuffer ? item.indexBuffer->getByteSize() / sizeof(u32) : 0;

  auto modelOpt = extractModelMatrix(item.drawData);
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
      << "  \"pipelineSrgbStatus\": \"" << jsonEscape(pipelineStatus)
      << "\",\n"
      << "  \"debug\": {\n"
      << "    \"profileAspect\": " << debugInfo.profileAspect << ",\n"
      << "    \"cameraAspect\": " << debugInfo.cameraAspect << ",\n"
      << "    \"cameraResourceCount\": " << debugInfo.cameraResourceCount
      << ",\n"
      << "    \"drawItemCount\": " << debugInfo.drawItemCount << ",\n"
      << "    \"viewportExtent\": {\"width\": "
      << debugInfo.viewportExtent.width << ", \"height\": "
      << debugInfo.viewportExtent.height << "},\n"
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
        << "\", \"objectSignature\": \""
        << jsonEscape(bounds.objectSignature) << "\", \"indexCount\": "
        << bounds.indexCount << ", \"valid\": "
        << (bounds.valid ? "true" : "false") << ", \"minX\": "
        << bounds.minX << ", \"minY\": " << bounds.minY
        << ", \"maxX\": " << bounds.maxX << ", \"maxY\": "
        << bounds.maxY << "}";
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

bool isSharedHostBufferResource(const IGpuResourceSharedPtr &resource) {
  if (!resource || !resource->isDirty()) {
    return false;
  }

  switch (resource->getType()) {
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

void validateOffscreenWritesMatchTarget(
    const LX_core::CompiledFrameGraphPass &pass) {
  const auto colorWrite =
      findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
  const auto depthWrite =
      findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Depth);

  if (pass.target.colorFormat.has_value() != colorWrite.has_value()) {
    throw std::runtime_error(
        "Frame graph offscreen pass color write does not match target");
  }
  if (pass.target.depthFormat.has_value() != depthWrite.has_value()) {
    throw std::runtime_error(
        "Frame graph offscreen pass depth write does not match target");
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
    guiParams.graphicsQueueFamilyIndex =
        device().getGraphicsQueueFamilyIndex();
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
  void setPostProcessSettings(
      const VulkanPostProcessSettings &settings) {
    m_postProcessSettings = settings;
  }
  [[nodiscard]] const VulkanPostProcessSettings &
  postProcessSettings() const {
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
    auto resources = m_scene->getIblEnvironmentResourceSet();
    if (!resources.equirectangularMap || resources.bakedSkyboxCubemap) {
      return;
    }

    LX_core::backend::IblBakeRenderer baker(device(), resourceManager(),
                                            commandBufferManager());
    const u32 prefilterMipCount = std::max(
        1u, static_cast<u32>(std::round(
                resources.environmentUbo
                    ? resources.environmentUbo->getPrefilteredMipCount()
                    : 1.0f)));
    const auto baked = baker.bakeStaticEnvironment({
        .equirectangularMap = resources.equirectangularMap,
        .skyboxSize = 64,
        .irradianceSize = 32,
        .prefilterSize = 64,
        .prefilterMipCount = prefilterMipCount,
        .brdfLutSize = 128,
    });

    resources.bakedSkyboxCubemap = baked.skybox;
    resources.bakedIrradianceCubemap = baked.irradiance;
    resources.bakedPrefilteredRadianceCubemap = baked.prefiltered;
    resources.bakedBrdfLut = baked.brdfLut;
    m_scene->setIblEnvironmentResources(std::move(resources));
  }

  void initScene(SceneSharedPtr _scene) {
    ++m_initSceneCallCount;
    if (m_swapchain) {
      m_swapchain->waitForAllFrames();
    }
    m_scene = _scene;

    // REQ-009 / REQ-046: compute the swapchain target once, then migrate
    // default scene cameras to the HDR forward target before buildFromScene so
    // scene-level CameraUBO resources still attach to the Forward queue.
    const LX_core::RenderTarget swapchainTarget = makeSwapchainTarget();
    for (const auto &cameraNode : m_scene->getCameras()) {
      if (!cameraNode) {
        continue;
      }
      const auto cameraComponent =
          cameraNode->getComponent<LX_core::CameraComponent>();
      if (cameraComponent && !cameraComponent->get().getTarget().has_value()) {
        cameraComponent->get().setTarget(swapchainTarget);
      }
    }

    const auto swapchainDesc = swapchainTarget.toDesc();
    LX_core::RenderTargetDesc forwardHdrDesc;
    forwardHdrDesc.role = LX_core::RenderTargetRole::Offscreen;
    forwardHdrDesc.colorFormat = LX_core::ImageFormat::RGBA16Float;
    forwardHdrDesc.depthFormat = swapchainTarget.depthFormat;
    const auto shadowTarget =
        LX_core::RenderTargetDesc::offscreenDepth(swapchainTarget.depthFormat);

    for (const auto &cameraNode : m_scene->getCameras()) {
      if (!cameraNode) {
        continue;
      }
      const auto cameraComponent =
          cameraNode->getComponent<LX_core::CameraComponent>();
      if (cameraComponent && cameraComponent->get().getTarget().has_value() &&
          *cameraComponent->get().getTarget() == swapchainTarget) {
        cameraComponent->get().setTarget(LX_core::RenderTarget{forwardHdrDesc});
      }
    }

    updateDirectionalLightCascades();
    bakeSceneIblEnvironmentIfNeeded();
    auto &forwardRenderPass = resourceManager().getRenderPass(forwardHdrDesc);
    forwardRenderPass.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    const auto sceneHdrColor = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("scene.hdrColor"));
    const auto sceneDepth = LX_core::FrameGraphResourceRef::depthAttachment(
        LX_core::StringID("scene.depth"));
    const auto bloomThreshold = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("bloom.threshold"));
    const auto bloomBlurH = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("bloom.blurH"));
    const auto bloomBlur = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("bloom.blur"));
    const auto swapchainColor = LX_core::FrameGraphResourceRef::colorAttachment(
        LX_core::StringID("swapchain.color"));

    m_frameGraph = LX_core::FrameGraph{}; // Fresh graph on every initScene.
    std::vector<LX_core::FrameGraphRead> shadowReads;
    shadowReads.reserve(LX_core::MaxShadowCascades);
    for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
         ++cascadeIndex) {
      const auto shadowDepth = LX_core::FrameGraphResourceRef::depthAttachment(
          LX_core::StringID("shadow.cascade" + std::to_string(cascadeIndex)));
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
    m_frameGraph.addPass(
        LX_core::FramePass{LX_core::Pass_Forward,
                           forwardHdrDesc,
                           {},
                           shadowReads,
                           {LX_core::FrameGraphWrite{sceneHdrColor},
                            LX_core::FrameGraphWrite{sceneDepth}}});
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
    const auto postBloomInput =
        m_postProcessSettings.bloomEnabled ? bloomBlur.name : sceneHdrColor.name;
    m_frameGraph.addPass(LX_core::FramePass{
        LX_core::Pass_PostProcess,
        swapchainDesc,
        {},
        {LX_core::FrameGraphRead::sampled(sceneHdrColor.name,
                                          LX_core::StringID("SceneColor")),
         LX_core::FrameGraphRead::sampled(postBloomInput,
                                          LX_core::StringID("BloomColor"))},
        {LX_core::FrameGraphWrite{swapchainColor}}});
    m_frameGraph.addPass(LX_core::FramePass{
        LX_core::Pass_DebugOverlay, swapchainDesc, {}, {}, {}});

    // RenderQueue::buildFromScene (invoked per pass below) internally:
    //   - filters renderables by supportsPass(pass)
    //   - merges scene.getSceneLevelResources(pass, target) (camera UBO
    //   filtered by
    //     target, light UBO filtered by pass mask)
    //   - sorts by PipelineKey
    // There is no more side-channel camera/light UBO injection here.
    m_frameGraph.buildFromScene(*m_scene);
    addSkyboxBackgroundItem(forwardHdrDesc);
    rebuildDebugOverlayQueueWithForwardCameraResources(forwardHdrDesc,
                                                       swapchainDesc);
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
      for (auto &item : pass.queue.getItems()) {
        resourceManager().syncResource(commandBufferManager(), item.vertexBuffer);
        resourceManager().syncResource(commandBufferManager(), item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          resourceManager().syncResource(commandBufferManager(), cpuRes);
        }
      }
    }
    resourceManager().collectGarbage();

    // Pre-build every pipeline the scene needs. Runtime cache misses still
    // work via getOrCreateRenderPipeline(item) but emit a warning log.
    auto infos = m_frameGraph.collectAllPipelineBuildDescs();
    resourceManager().preloadPipelines(infos);
  }

  void uploadData() {
    updateDirectionalLightCascades();

    const u32 currentFrameIndex = m_frameIndex % kMaxFramesInFlight;
    resourceManager().beginFrame(currentFrameIndex);
    bool requiresSharedBufferSync = false;
    for (auto &pass : m_frameGraph.getPasses()) {
      for (auto &item : pass.queue.getItems()) {
        requiresSharedBufferSync =
            requiresSharedBufferSync ||
            isSharedHostBufferResource(item.vertexBuffer) ||
            isSharedHostBufferResource(item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          requiresSharedBufferSync =
              requiresSharedBufferSync || isSharedHostBufferResource(cpuRes);
        }
        if (requiresSharedBufferSync) {
          break;
        }
      }
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
      for (auto &item : pass.queue.getItems()) {
        resourceManager().syncResource(commandBufferManager(), item.vertexBuffer);
        resourceManager().syncResource(commandBufferManager(), item.indexBuffer);
        for (auto &cpuRes : item.descriptorResources) {
          resourceManager().syncResource(commandBufferManager(), cpuRes);
        }
      }
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
    return m_foundation ? resourceManager().getFrameGraphAttachmentCount()
                             : 0;
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
    };
  }

  VulkanFrameGraphAttachmentDumpResult dumpDebugRenderTarget(
      std::string_view passName, const std::optional<std::string> &cameraPath,
      const std::optional<std::filesystem::path> &requestedPath) {
    if (!m_foundation || !m_swapchain ||
        !m_scene) {
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
    const auto previousTarget = cameraComponent.getTarget();
    cameraComponent.setTarget(target);
    cameraComponent.updateMatrices();
    updateDirectionalLightCascadesForCamera(cameraComponent);
    auto sceneResources = m_scene->getSceneLevelResources(pass, target);
    cameraComponent.setTarget(previousTarget);

    if (pass == LX_core::Pass_Forward) {
      for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
           ++cascadeIndex) {
        const auto shadowDepth =
            LX_core::FrameGraphResourceRef::depthAttachment(LX_core::StringID(
                "shadow.cascade" + std::to_string(cascadeIndex)));
        sceneResources.push_back(
            std::make_shared<LX_core::FrameGraphSampledResource>(
                shadowDepth.name,
                LX_core::StringID("ShadowMap" + std::to_string(cascadeIndex))));
      }
    }

    LX_core::RenderQueue queue;
    queue.buildFromSceneWithOverrides(
        *m_scene, pass, target, std::move(sceneResources),
        cameraComponent.getCullingMask() & ~LX_core::Layer_EditorOverlay);
    if (queue.getItems().empty()) {
      throw std::runtime_error("debug render target produced no draw items");
    }

    for (auto &item : queue.getItems()) {
      resourceManager().syncResource(commandBufferManager(), item.vertexBuffer);
      resourceManager().syncResource(commandBufferManager(), item.indexBuffer);
      for (auto &cpuRes : item.descriptorResources) {
        resourceManager().syncResource(commandBufferManager(), cpuRes);
      }
    }
    resourceManager().preloadPipelines(
        queue.collectUniquePipelineBuildDescs());

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
      auto &pipeline = resourceManager().getOrCreateRenderPipeline(item);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(resourceManager(), pipeline, item);
      cmd->drawItem(item);
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

  VulkanRealtimeProfileOutputResult generateRealtimeProfileOutput(
      SceneSharedPtr scene, const LX_core::offline::OutputProfile &output,
      const std::filesystem::path &basePath) {
    if (!m_foundation || !m_swapchain) {
      throw std::runtime_error("renderer is not initialized");
    }
    if (!scene) {
      throw std::runtime_error("realtime profile output requires a scene");
    }
    if (output.width == 0 || output.height == 0) {
      throw std::runtime_error("realtime profile output extent must be positive");
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
    auto &camera = cameraOpt->get();

    struct CameraRestore final {
      LX_core::CameraComponent &camera;
      std::optional<LX_core::RenderTarget> target;
      LX_core::CameraType projectionType;
      float fovY;
      float aspect;
      float nearPlane;
      float farPlane;
      float left;
      float right;
      float bottom;
      float top;
      LX_core::VisibilityLayerMask cullingMask;
      ~CameraRestore() {
        camera.setTarget(target);
        camera.applyProjectionState(projectionType, fovY, aspect, nearPlane,
                                    farPlane, left, right, bottom, top);
        camera.setCullingMask(cullingMask);
        camera.updateMatrices();
      }
    } restore{
        .camera = camera,
        .target = camera.getTarget(),
        .projectionType = camera.getProjectionType(),
        .fovY = camera.getFovY(),
        .aspect = camera.getAspect(),
        .nearPlane = camera.getNearPlane(),
        .farPlane = camera.getFarPlane(),
        .left = camera.getLeft(),
        .right = camera.getRight(),
        .bottom = camera.getBottom(),
        .top = camera.getTop(),
        .cullingMask = camera.getCullingMask(),
    };

    const float profileAspect =
        static_cast<float>(output.width) / static_cast<float>(output.height);
    camera.setAspect(output.cameraOverrides.aspect.value_or(profileAspect));
    const float resolvedAspect = camera.getAspect();
    if (output.cameraOverrides.fovY.has_value()) {
      camera.setFovY(*output.cameraOverrides.fovY);
    }
    if (output.cameraOverrides.nearPlane.has_value()) {
      camera.setNearPlane(*output.cameraOverrides.nearPlane);
    }
    if (output.cameraOverrides.farPlane.has_value()) {
      camera.setFarPlane(*output.cameraOverrides.farPlane);
    }
    if (output.cameraOverrides.cullingMask.has_value()) {
      camera.setCullingMask(*output.cameraOverrides.cullingMask);
    }
    if (camera.getProjectionType() == LX_core::CameraType::Orthographic) {
      const float currentHeight = std::max(camera.getTop() - camera.getBottom(),
                                           0.0001f);
      const float orthoHeight =
          output.cameraOverrides.orthographicHeight.value_or(currentHeight);
      const float orthoWidth = orthoHeight * resolvedAspect;
      camera.setOrthographicBounds(-orthoWidth * 0.5f, orthoWidth * 0.5f,
                                   -orthoHeight * 0.5f, orthoHeight * 0.5f);
    }

    LX_core::RenderTargetDesc targetDesc;
    targetDesc.role = LX_core::RenderTargetRole::Offscreen;
    targetDesc.colorFormat = LX_core::ImageFormat::RGBA16Float;
    targetDesc.depthFormat = LX_core::ImageFormat::D32Float;
    const LX_core::RenderTarget target{targetDesc};
    camera.setTarget(target);
    camera.updateMatrices();
    updateDirectionalLightCascadesForCamera(camera);

    auto sceneResources =
        m_scene->getSceneLevelResources(LX_core::Pass_Forward, target);
    RealtimeProfileDebugInfo debugInfo;
    debugInfo.profileAspect = profileAspect;
    debugInfo.cameraAspect = camera.getAspect();
    debugInfo.cameraView = camera.getUBO()->param.view;
    debugInfo.cameraProj = camera.getUBO()->param.proj;
    debugInfo.cameraResourceCount = countCameraResources(sceneResources);

    const std::string attachmentPrefix =
        "realtime.profile." + basePath.generic_string() + "." +
        std::to_string(output.width) + "x" + std::to_string(output.height);
    const auto shadowFallbackRef =
        LX_core::FrameGraphResourceRef::depthAttachment(
            LX_core::StringID(attachmentPrefix + ".shadowFallback"));
    for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
         ++cascadeIndex) {
      sceneResources.push_back(
          std::make_shared<LX_core::FrameGraphSampledResource>(
              shadowFallbackRef.name,
              LX_core::StringID("ShadowMap" + std::to_string(cascadeIndex))));
    }

    LX_core::RenderQueue queue;
    queue.buildFromSceneWithOverrides(
        *m_scene, LX_core::Pass_Forward, target, std::move(sceneResources),
        camera.getCullingMask() & ~LX_core::Layer_EditorOverlay);
    if (queue.getItems().empty()) {
      throw std::runtime_error("realtime profile output produced no draw items");
    }
    debugInfo.drawItemCount = static_cast<u32>(queue.getItems().size());
    const LX_core::Mat4f viewProj =
        camera.getUBO()->param.proj * camera.getUBO()->param.view;
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
    for (auto &item : queue.getItems()) {
      resourceManager().syncResource(commandBufferManager(), item.vertexBuffer);
      resourceManager().syncResource(commandBufferManager(), item.indexBuffer);
      for (auto &cpuRes : item.descriptorResources) {
        resourceManager().syncResource(commandBufferManager(), cpuRes);
      }
    }
    resourceManager().preloadPipelines(
        queue.collectUniquePipelineBuildDescs());

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
    auto &shadowFallbackAttachment =
        resourceManager().createOrGetFrameGraphAttachment(
            shadowFallbackRef.name, VkExtent2D{1, 1}, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT);
    std::unique_ptr<VulkanFrameBuffer> shadowFallbackFramebuffer;
    transitionFrameGraphAttachment(
        shadowFallbackRef, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, *cmd);
    {
      LX_core::RenderTargetDesc shadowFallbackDesc =
          LX_core::RenderTargetDesc::offscreenDepth(
              LX_core::ImageFormat::D32Float);
      auto &shadowFallbackRenderPass =
          resourceManager().getRenderPass(shadowFallbackDesc);
      std::vector<VkImageView> shadowFallbackViews{
          shadowFallbackAttachment.texture->getImageView()};
      shadowFallbackFramebuffer = VulkanFrameBuffer::create(
          device(), shadowFallbackRenderPass.getHandle(), shadowFallbackViews,
          VkExtent2D{1, 1});
      cmd->beginRenderPass(shadowFallbackRenderPass.getHandle(),
                           shadowFallbackFramebuffer->getHandle(),
                           VkExtent2D{1, 1},
                           shadowFallbackRenderPass.getClearValues());
      cmd->setViewport(1, 1);
      cmd->setScissor(1, 1);
      cmd->endRenderPass();
    }
    transitionFrameGraphAttachment(
        shadowFallbackRef, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, *cmd);

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
    cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                         extent, renderPass.getClearValues());
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    for (auto &item : queue.getItems()) {
      auto &pipeline = resourceManager().getOrCreateRenderPipeline(item);
      cmd->bindPipeline(pipeline);
      cmd->bindResources(resourceManager(), pipeline, item);
      cmd->drawItem(item);
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
    LX_core::offline::OfflineReadbackImage image =
        makeRgba32fImageFromDump(colorFormat, output.width, output.height,
                                 mapped);
    readback->unmap();

    const std::filesystem::path outputDir = basePath.parent_path();
    std::filesystem::create_directories(outputDir);
    const std::string outputStem =
        basePath.filename().empty() ? std::string("render")
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
    if (passIndex >= m_frameGraph.getPasses().size()) {
      return;
    }

    for (auto &item : m_frameGraph.getPasses()[passIndex].queue.getItems()) {
      auto &pipeline = resourceManager().getOrCreateRenderPipeline(item);
      cmd.bindPipeline(pipeline);
      cmd.bindResources(resourceManager(), pipeline, item);
      cmd.drawItem(item);
    }
  }

  void addFullscreenMaterialItem(LX_core::StringID pass,
                                 const LX_core::RenderTargetDesc &target,
                                 LX_core::MaterialInstanceSharedPtr material,
                                 const char *objectSignature) {
    LX_core::RenderingItem item;
    item.shaderInfo = material->getPassShader(pass);
    item.material = material;
    item.vertexBuffer = LX_core::VertexBuffer<LX_core::VertexPos>::create(
        std::vector<LX_core::VertexPos>{{{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}}});
    item.indexBuffer = LX_core::IndexBuffer::create({0u, 1u, 2u});
    item.descriptorResources = material->getDescriptorResources(pass);
    item.pass = pass;
    item.target = target;
    item.objectSignature = LX_core::StringID(objectSignature);
    item.materialSignature = material->getPipelineSignature(pass);
    item.pipelineKey =
        LX_core::PipelineKey::build(item.objectSignature, item.materialSignature,
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
    addFullscreenMaterialItem(
        LX_core::Pass_BloomThreshold,
        LX_core::RenderTargetDesc::offscreenColor(LX_core::ImageFormat::RGBA16Float),
        builder.createBloomThresholdMaterial(), "BloomThresholdFullscreenTriangle");
  }

  void addBloomBlurItem(LX_core::StringID pass, const char *shaderName,
                        const char *objectSignature) {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    addFullscreenMaterialItem(
        pass,
        LX_core::RenderTargetDesc::offscreenColor(LX_core::ImageFormat::RGBA16Float),
        builder.createBloomBlurMaterial(pass, shaderName), objectSignature);
  }

  void addStandardPostProcessItem(const LX_core::RenderTargetDesc &target) {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    addFullscreenMaterialItem(LX_core::Pass_PostProcess, target,
                              builder.createStandardPostProcessMaterial(),
                              "PostProcessFullscreenTriangle");
  }

  void addSkyboxBackgroundItem(const LX_core::RenderTargetDesc &target) {
    if (!m_scene) {
      return;
    }
    const auto iblResources = m_scene->getIblEnvironmentResourceSet();
    const auto skyboxResource = iblResources.bakedSkyboxCubemap
                                    ? iblResources.bakedSkyboxCubemap
                                    : std::static_pointer_cast<
                                          LX_core::IGpuResource>(
                                          iblResources.skyboxCubemap);
    if (!skyboxResource || !iblResources.environmentUbo ||
        iblResources.environmentUbo->getIblIntensity() <= 0.0f) {
      return;
    }

    VulkanPostProcessBuilder builder(m_postProcessSettings);
    const auto material = builder.createSkyboxBackgroundMaterial();
    LX_core::RenderingItem item;
    item.shaderInfo = material->getPassShader(LX_core::Pass_Forward);
    item.material = material;
    item.vertexBuffer = LX_core::VertexBuffer<LX_core::VertexPos>::create(
        std::vector<LX_core::VertexPos>{{{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}},
                                        {{0.0f, 0.0f, 0.0f}}});
    item.indexBuffer = LX_core::IndexBuffer::create({0u, 1u, 2u});
    item.descriptorResources =
        material->getDescriptorResources(LX_core::Pass_Forward);
    const LX_core::RenderTarget renderTarget{target};
    auto sceneResources =
        m_scene->getSceneLevelResources(LX_core::Pass_Forward, renderTarget);
    item.descriptorResources.insert(item.descriptorResources.end(),
                                    sceneResources.begin(),
                                    sceneResources.end());
    item.descriptorResources.push_back(skyboxResource);
    item.descriptorResources.push_back(iblResources.environmentUbo);
    item.pass = LX_core::Pass_Forward;
    item.target = target;
    item.objectSignature = LX_core::StringID("SkyboxFullscreenTriangle");
    item.materialSignature = material->getPipelineSignature(item.pass);
    item.pipelineKey =
        LX_core::PipelineKey::build(item.objectSignature, item.materialSignature,
                                    item.target.getPipelineSignature());

    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == LX_core::Pass_Forward) {
        pass.queue.addItem(std::move(item));
        pass.queue.sort();
        return;
      }
    }
  }

  void rebuildDebugOverlayQueueWithForwardCameraResources(
      const LX_core::RenderTargetDesc &forwardTarget,
      const LX_core::RenderTargetDesc &debugTarget) {
    if (!m_scene) {
      return;
    }
    const LX_core::RenderTarget forwardRenderTarget{forwardTarget};
    const LX_core::RenderTarget debugRenderTarget{debugTarget};
    auto sceneResources = m_scene->getSceneLevelResources(
        LX_core::Pass_DebugOverlay, forwardRenderTarget);
    const auto visibleMask =
        m_scene->getCombinedCameraCullingMask(forwardRenderTarget);
    for (auto &pass : m_frameGraph.getPasses()) {
      if (pass.name == LX_core::Pass_DebugOverlay) {
        pass.queue.buildFromSceneWithOverrides(
            *m_scene, LX_core::Pass_DebugOverlay, debugRenderTarget,
            std::move(sceneResources), visibleMask);
        return;
      }
    }
  }

  LX_core::DirectionalLightSharedPtr mainDirectionalLight() const {
    if (!m_scene) {
      return nullptr;
    }
    for (const auto &light : m_scene->getLights()) {
      if (auto directional =
              std::dynamic_pointer_cast<LX_core::DirectionalLight>(light)) {
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
                                      m_shadowCascadeUboSnapshots[cascadeIndex]);
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

  void refreshShadowCascadeUboSnapshots(const LX_core::DirectionalLight &light) {
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
          light.getDirectionalUBO()->param;
      m_shadowCascadeUboSnapshots[cascadeIndex]->param.shadowViewProj =
          light.getDirectionalUBO()->param.cascadeViewProj[cascadeIndex];
      m_shadowCascadeUboSnapshots[cascadeIndex]->setDirty();
    }
  }

  void bindShadowCascadeUboSnapshots() {
    const auto light = mainDirectionalLight();
    if (!light || m_shadowCascadeUboSnapshots.empty()) {
      return;
    }

    const auto mainLightIdentity = light->getUBO()->getBackendCacheIdentity();
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
          if (resource &&
              resource->getBackendCacheIdentity() == mainLightIdentity) {
            resource = snapshot;
          }
        }
      }
      ++cascadeIndex;
    }
  }

  void attachFrameGraphSampledResources() {
    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    auto &graphPasses = m_frameGraph.getPasses();
    const usize passCount = std::min(compiledPasses.size(), graphPasses.size());
    for (usize passIndex = 0; passIndex < passCount; ++passIndex) {
      for (const auto &read : compiledPasses[passIndex].reads) {
        if (read.bindingName == LX_core::StringID{}) {
          continue;
        }
        auto resource = std::make_shared<LX_core::FrameGraphSampledResource>(
            read.resource, read.bindingName);
        for (auto &item : graphPasses[passIndex].queue.getItems()) {
          item.descriptorResources.push_back(resource);
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
    attachments.reserve(2);
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

    if (pass.target.colorFormat.has_value()) {
      const auto write =
          findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
      appendAttachment(write->get(), *pass.target.colorFormat);
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
  std::vector<LX_core::DirectionalLightDataSharedPtr>
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
