#include "vulkan_realtime_renderer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/frame_graph_build_plan.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/frame_graph/render_path_feature_validation.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_render_result.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/light.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "core/utils/hash.hpp"
#include "core/utils/string_table.hpp"
#include "infra/gui/gui.hpp"
#include "infra/image/rgba_image_io.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
#include "infra/resource_parsers/material_source_variant_resolver.hpp"
#include "infra/resource_parsers/render_resource_scene_parser_adapters.hpp"
#include "infra/resource_parsers/scene_resource_parser_registry.hpp"
#include "infra/window/window.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/descriptors/descriptor_manager.hpp"
#include "details/device.hpp"
#include "details/device_resources/buffer.hpp"
#include "details/device_resources/texture.hpp"
#include "details/render_objects/framebuffer.hpp"
#include "details/render_objects/render_pass.hpp"
#include "details/render_objects/swapchain.hpp"
#include "details/resource_manager.hpp"
#include "vulkan_frame_graph_executor.hpp"
#include "vulkan_post_process_builder.hpp"
#include "vulkan_renderer_foundation.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
namespace {
constexpr const char *kBloomBlurHShaderName = "render_paths/Post/bloom_blur_h";
constexpr const char *kBloomBlurVShaderName = "render_paths/Post/bloom_blur_v";
constexpr const char *kDefaultForwardRenderPathGraphAsset =
    "assets/render_paths/forward_main.render-path.yaml";
constexpr const char *kDefaultForwardBloomRenderPathGraphAsset =
    "assets/render_paths/forward_main.render-path.yaml";
constexpr const char *kDefaultDeferredRenderPathGraphAsset =
    "assets/render_paths/deferred_main.render-path.yaml";
constexpr const char *kDefaultDeferredBloomRenderPathGraphAsset =
    "assets/render_paths/deferred_bloom.render-path.yaml";

bool strictBindlessValidationEnabled() {
  return expEnvEnabled("LXE_STRICT_BINDLESS_VALIDATION");
}

bool isMigratedBindlessValidationPass(LX_core::StringID pass) {
  return pass == LX_core::Pass_Forward || pass == LX_core::Pass_Deferred;
}

LX_core::ShaderStageCode loadRuntimeShaderStage(const std::string &shaderName,
                                                const char *suffix,
                                                LX_core::ShaderStage stage) {
  const auto bytes =
      readFile((getRuntimeShaderBinaryDir() / (shaderName + "." + suffix))
                   .string());
  if ((bytes.size() % sizeof(uint32_t)) != 0) {
    throw std::runtime_error("shader bytecode size is not 4-byte aligned: " +
                             shaderName + "." + suffix);
  }

  LX_core::ShaderStageCode code;
  code.stage = stage;
  code.bytecode.resize(bytes.size() / sizeof(uint32_t));
  std::memcpy(code.bytecode.data(), bytes.data(), bytes.size());
  return code;
}

LX_core::IShaderSharedPtr
loadRuntimeGraphicsShaderPayload(const LX_core::ResourceUri &shaderUri) {
  const std::string shaderName = shaderUri.string();
  std::vector<LX_core::ShaderStageCode> stages{
      loadRuntimeShaderStage(shaderName, "vert.spv", LX_core::ShaderStage::Vertex),
      loadRuntimeShaderStage(shaderName, "frag.spv",
                             LX_core::ShaderStage::Fragment),
  };
  auto bindings = LX_infra::ShaderReflector::reflect(stages);
  auto vertexInputs = LX_infra::ShaderReflector::reflectVertexInputs(stages);
  auto specializationConstants =
      LX_infra::ShaderReflector::reflectSpecializationConstants(stages);
  return std::make_shared<LX_infra::CompiledShader>(
      std::move(stages), std::move(bindings), std::move(vertexInputs),
      std::move(specializationConstants), shaderName);
}

LX_core::RenderPathGraph
loadRenderPathGraphAsset(LX_core::Scene &scene, std::string_view assetPath,
                         LX_core::RenderPath expectedRenderPath) {
  LX_infra::SceneResourceParserRegistry registry;
  LX_infra::registerRenderResourceParsers(registry);
  const LX_core::ResourceUri graphUri{std::string(assetPath)};
  const LX_infra::ParsedSceneResource parsed = registry.parse(
      scene.resources(), LX_core::SceneResourceType::RenderPathGraph, graphUri,
      LX_infra::SceneResourceParseContext{
          .ownerUri = LX_core::ResourceUri("realtime-render://frame-graph")});
  if (!parsed.identity.isValid() ||
      parsed.metadata.state == LX_core::ResourceState::Failed) {
    std::string message =
        "failed to load realtime RenderPathGraph '" + graphUri.string() + "'";
    for (const std::string &diagnostic : parsed.diagnostics) {
      message += "\n  ";
      message += diagnostic;
    }
    throw std::runtime_error(message);
  }

  const auto graphHandle =
      scene.resources().findRenderPathGraphByMetadataHandle(parsed.identity);
  if (!graphHandle.has_value()) {
    throw std::runtime_error("realtime RenderPathGraph did not register "
                             "payload for '" +
                             graphUri.string() + "'");
  }
  const auto graph = scene.resources().resolve(*graphHandle);
  if (!graph.has_value()) {
    throw std::runtime_error("realtime RenderPathGraph payload is not "
                             "resolvable for '" +
                             graphUri.string() + "'");
  }
  if (graph->get().renderPath != expectedRenderPath) {
    throw std::runtime_error("RenderPathGraph asset declares the wrong "
                             "renderPath: " +
                             graphUri.string());
  }
  return graph->get();
}

void applyToneMappingFeatureSettings(
    const LX_core::SceneResourceTable &resources,
    const LX_core::RenderPathGraph &graph,
    LX_core::backend::VulkanPostProcessSettings &settings) {
  for (const auto &featureDependency : graph.features) {
    if (featureDependency.slot != "toneMapping") {
      continue;
    }
    const auto handle = resources.findRenderFeatureByUri(featureDependency.uri);
    if (!handle.has_value()) {
      throw std::runtime_error("toneMapping RenderFeature was not registered: " +
                               featureDependency.uri.string());
    }
    const auto feature = resources.resolve(*handle);
    if (!feature.has_value()) {
      throw std::runtime_error("toneMapping RenderFeature payload is not "
                               "resolvable: " +
                               featureDependency.uri.string());
    }
    const auto exposureIt = feature->get().parameters.find("exposure");
    if (exposureIt != feature->get().parameters.end() &&
        !exposureIt->second.value.empty()) {
      settings.exposure = std::stof(exposureIt->second.value);
    }
    return;
  }
}

void resolveMaterialSourceVariantsOrThrow(
    LX_core::Scene &scene, const LX_core::RenderPathGraph &graph,
    const LX_core::ResourceUri &graphUri) {
  const LX_infra::MaterialSourceVariantResolverResult resolved =
      LX_infra::resolveMaterialSourceVariants(scene.resources(), graph,
                                              graphUri);
  if (resolved.success) {
    scene.rebuildRenderableCaches();
    return;
  }

  std::string message =
      "failed to resolve material source shader variants for RenderPathGraph " +
      graphUri.string();
  for (const std::string &diagnostic : resolved.diagnostics) {
    message += "\n  ";
    message += diagnostic;
  }
  throw std::runtime_error(message);
}

/// REQ-009: reverse of resource_manager.cpp's toVkFormat(ImageFormat).
/// Only covers the swapchain-relevant VkFormats. Unknown inputs fall back to
/// RGBA8 and log a debug warning rather than throwing — initScene must be
/// robust against whatever surface format the Vulkan driver exposes.
LX_core::ImageFormat toImageFormat(VkFormat format) {
  switch (format) {
  case VK_FORMAT_B8G8R8A8_SRGB:
    return LX_core::ImageFormat::BGRA8Srgb;
  case VK_FORMAT_B8G8R8A8_UNORM:
    return LX_core::ImageFormat::BGRA8;
  case VK_FORMAT_R8G8B8A8_SRGB:
    return LX_core::ImageFormat::RGBA8Srgb;
  case VK_FORMAT_R8G8B8A8_UNORM:
    return LX_core::ImageFormat::RGBA8;
  case VK_FORMAT_R16G16_SFLOAT:
    return LX_core::ImageFormat::RG16Float;
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
  case LX_core::ImageFormat::RGBA8Srgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case LX_core::ImageFormat::RG16Float:
    return VK_FORMAT_R16G16_SFLOAT;
  case LX_core::ImageFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case LX_core::ImageFormat::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case LX_core::ImageFormat::BGRA8Srgb:
    return VK_FORMAT_B8G8R8A8_SRGB;
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
  case VK_FORMAT_R8G8B8A8_SRGB:
    return "R8G8B8A8_SRGB";
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return "R16G16B16A16_SFLOAT";
  case VK_FORMAT_R32G32B32A32_SFLOAT:
    return "R32G32B32A32_SFLOAT";
  case VK_FORMAT_B8G8R8A8_UNORM:
    return "B8G8R8A8_UNORM";
  case VK_FORMAT_B8G8R8A8_SRGB:
    return "B8G8R8A8_SRGB";
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
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_SRGB:
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

  if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
      format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM) {
    const auto *rgba = static_cast<const unsigned char *>(mappedData);
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const usize i =
            (static_cast<usize>(y) * width + static_cast<usize>(x)) * 4u;
        if (format == VK_FORMAT_B8G8R8A8_UNORM ||
            format == VK_FORMAT_B8G8R8A8_SRGB) {
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
             format == VK_FORMAT_R8G8B8A8_SRGB ||
             format == VK_FORMAT_B8G8R8A8_SRGB ||
             format == VK_FORMAT_B8G8R8A8_UNORM) {
    const auto *pixels = static_cast<const unsigned char *>(mappedData);
    const bool sourceIsBgra =
        format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    for (usize i = 0; i < pixelCount; ++i) {
      const usize base = i * 4u;
      const double r = pixels[base + (sourceIsBgra ? 2u : 0u)] / 255.0;
      const double g = pixels[base + 1u] / 255.0;
      const double b = pixels[base + (sourceIsBgra ? 0u : 2u)] / 255.0;
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
  stats.nonZeroRatio =
      static_cast<double>(nonZeroCount) / static_cast<double>(pixelCount);
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

std::vector<unsigned char> makeRgbaPixelsFromDump(VkFormat format, u32 width,
                                                  u32 height,
                                                  const void *mappedData) {
  const usize pixelCount = static_cast<usize>(width) * height;
  std::vector<unsigned char> rgba(pixelCount * 4u);
  if (format != VK_FORMAT_R8G8B8A8_UNORM &&
      format != VK_FORMAT_R8G8B8A8_SRGB &&
      format != VK_FORMAT_B8G8R8A8_UNORM &&
      format != VK_FORMAT_B8G8R8A8_SRGB) {
    throw std::runtime_error("realtime final output does not support " +
                             vkFormatName(format));
  }
  const auto *pixels = static_cast<const unsigned char *>(mappedData);
  const bool sourceIsBgra =
      format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
  for (usize i = 0; i < pixelCount; ++i) {
    const usize base = i * 4u;
    rgba[base + 0u] = pixels[base + (sourceIsBgra ? 2u : 0u)];
    rgba[base + 1u] = pixels[base + 1u];
    rgba[base + 2u] = pixels[base + (sourceIsBgra ? 0u : 2u)];
    rgba[base + 3u] = pixels[base + 3u];
  }
  return rgba;
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

struct PipelineIdentityDebug final {
  std::string materialTypeVariant;
  std::string renderPathNodeSignature;
  std::string pipelineKey;
  std::string shaderName;
  std::vector<std::string> finalShaderReflection;
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
  std::vector<PipelineIdentityDebug> pipelineIdentity;
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

[[nodiscard]] std::string jsonEscape(std::string_view text);

[[nodiscard]] std::string debugString(LX_core::StringID id) {
  if (id.id == 0) {
    return "<empty>";
  }
  return LX_core::GlobalStringTable::get().toDebugString(id);
}

void writeStringArrayJson(std::ostream &out,
                          const std::vector<std::string> &values) {
  out << "[";
  for (usize i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << "\"" << jsonEscape(values[i]) << "\"";
  }
  out << "]";
}

[[nodiscard]] PipelineIdentityDebug
makePipelineIdentityDebug(const LX_core::FramePass &pass,
                          const LX_core::RenderInputDesc &desc) {
  PipelineIdentityDebug out;
  out.materialTypeVariant =
      debugString(desc.pipelineBuildDesc.shaderVariantKey);
  out.renderPathNodeSignature =
      debugString(LX_core::getFramePassRenderPathNodeSignature(pass));
  out.pipelineKey = debugString(desc.pipelineKey.id);
  out.shaderName = debugString(desc.shaderUri);
  for (const auto &binding : desc.pipelineBuildDesc.bindings) {
    out.finalShaderReflection.push_back(binding.name);
  }
  return out;
}

[[nodiscard]] usize
executableDrawCommandCount(const LX_core::RenderDrawInput &draw) {
  if (draw.source == LX_core::RenderDrawInputSource::FullscreenTriangle) {
    return 1;
  }
  return static_cast<usize>(std::count_if(
      draw.drawCommands.begin(), draw.drawCommands.end(),
      [](const LX_core::RenderDrawCommand &command) {
        return command.indexCount > 0 && command.instanceCount > 0;
      }));
}

void recordExecutedRenderInputStats(
    LX_core::backend::VulkanRealtimeRenderInputStats &stats,
    const LX_core::RenderInput &input) {
  if (const auto *draw =
          dynamic_cast<const LX_core::RenderDrawInput *>(&input)) {
    stats.submittedDrawCount += executableDrawCommandCount(*draw);
    return;
  }
  if (dynamic_cast<const LX_core::RenderComputeInput *>(&input) != nullptr) {
    ++stats.submittedDispatchCount;
  }
}

[[nodiscard]] LX_core::backend::VulkanRealtimeRenderInputStats
toRealtimeProfileInputStats(
    const LX_core::gpu::LiveRenderSubmissionStats &stats) {
  return LX_core::backend::VulkanRealtimeRenderInputStats{
      .compilerInputCount = stats.compilerInputCount,
      .acceptedInputCount = stats.acceptedInputCount,
      .rejectedInputCount = stats.rejectedInputCount,
      .submittedDrawCount = stats.submittedDrawCount,
      .submittedDispatchCount = stats.submittedDispatchCount,
      .fallbackObservedCount = stats.fallbackObservedCount,
      .descPipelineLookupCount = stats.descPipelineLookupCount,
      .descBoundInputCount = stats.descBoundInputCount,
      .descExecutedInputCount = stats.descExecutedInputCount,
  };
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

[[nodiscard]] std::unique_ptr<LX_core::CameraData>
makeProfileCameraUbo(const LX_core::CameraResource &camera) {
  auto ubo = std::make_unique<LX_core::CameraData>();
  ubo->param = LX_core::CameraData::Param{
      .view = camera.view,
      .proj = camera.proj,
      .eyePos = camera.pose.eye,
      .pad = 0.0f,
  };
  ubo->setDirty();
  return ubo;
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

[[nodiscard]] LX_core::Vec4f
findLightDirection(const LX_core::DescriptorResourceList &resources) {
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
      << "    \"pipelineIdentity\": [\n";
  for (usize i = 0; i < debugInfo.pipelineIdentity.size(); ++i) {
    const auto &pipeline = debugInfo.pipelineIdentity[i];
    out << "      {\"MaterialTypeVariant\": \""
        << jsonEscape(pipeline.materialTypeVariant)
        << "\", \"RenderPathNodeSignature\": \""
        << jsonEscape(pipeline.renderPathNodeSignature)
        << "\", \"PipelineKey\": \"" << jsonEscape(pipeline.pipelineKey)
        << "\", \"final shader reflection\": {\"shaderName\": \""
        << jsonEscape(pipeline.shaderName) << "\", \"bindings\": ";
    writeStringArrayJson(out, pipeline.finalShaderReflection);
    out << "}}";
    if (i + 1 < debugInfo.pipelineIdentity.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ],\n"
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
      << "  },\n"
      << "  \"renderInputStats\": {\n"
      << "    \"compilerInputCount\": "
      << result.renderInputStats.compilerInputCount << ",\n"
      << "    \"acceptedInputCount\": "
      << result.renderInputStats.acceptedInputCount << ",\n"
      << "    \"rejectedInputCount\": "
      << result.renderInputStats.rejectedInputCount << ",\n"
      << "    \"submittedDrawCount\": "
      << result.renderInputStats.submittedDrawCount << ",\n"
      << "    \"submittedDispatchCount\": "
      << result.renderInputStats.submittedDispatchCount << ",\n"
      << "    \"fallbackObservedCount\": "
      << result.renderInputStats.fallbackObservedCount << ",\n"
      << "    \"descPipelineLookupCount\": "
      << result.renderInputStats.descPipelineLookupCount << ",\n"
      << "    \"descBoundInputCount\": "
      << result.renderInputStats.descBoundInputCount << ",\n"
      << "    \"descExecutedInputCount\": "
      << result.renderInputStats.descExecutedInputCount << "\n"
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

std::string lowerExtension(const std::filesystem::path &path) {
  std::string extension = path.extension().generic_string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

std::vector<unsigned char>
makeRgbaPixelsFromBgr(u32 width, u32 height,
                      const std::vector<unsigned char> &bgrPixels) {
  const usize pixelCount = static_cast<usize>(width) * height;
  if (bgrPixels.size() != pixelCount * 3u) {
    throw std::runtime_error("invalid render target dump pixel payload");
  }
  std::vector<unsigned char> rgba(pixelCount * 4u);
  for (usize i = 0; i < pixelCount; ++i) {
    rgba[i * 4u + 0u] = bgrPixels[i * 3u + 2u];
    rgba[i * 4u + 1u] = bgrPixels[i * 3u + 1u];
    rgba[i * 4u + 2u] = bgrPixels[i * 3u + 0u];
    rgba[i * 4u + 3u] = 255u;
  }
  return rgba;
}

void writeDebugImageFile(const std::filesystem::path &path, u32 width,
                         u32 height,
                         const std::vector<unsigned char> &bgrPixels) {
  const std::string extension = lowerExtension(path);
  if (extension == ".bmp") {
    writeBmp24File(path, width, height, bgrPixels);
    return;
  }
  if (extension == ".png") {
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
    LX_infra::image::writeRawRgba8Png(
        path, width, height, makeRgbaPixelsFromBgr(width, height, bgrPixels));
    return;
  }
  throw std::runtime_error("render debug dump only supports .png or .bmp: " +
                           path.string());
}
} // namespace

namespace LX_core::backend {

PreparedRenderStateCacheDecision evaluatePreparedRenderStateCache(
    const PreparedRenderStateCacheSnapshot &current,
    const PreparedRenderStateKey &nextKey,
    const u64 nextDescriptorResourceSelectionGeneration,
    const u64 nextDescriptorUploadGeneration,
    const u64 nextVolatileUploadGeneration) {
  PreparedRenderStateCacheDecision decision;
  const bool graphDirty =
      !current.valid || current.key.graphGeneration != nextKey.graphGeneration ||
      current.key.target != nextKey.target;
  const bool renderInputsDirty =
      graphDirty || current.key.resourceGeneration != nextKey.resourceGeneration ||
      current.key.featureGeneration != nextKey.featureGeneration ||
      current.key.sceneNodeGeneration != nextKey.sceneNodeGeneration;
  const bool descriptorResourceSelectionDirty =
      renderInputsDirty || current.descriptorResourceSelectionGeneration !=
                              nextDescriptorResourceSelectionGeneration;
  const bool descriptorUploadDirty =
      descriptorResourceSelectionDirty ||
      current.descriptorUploadGeneration != nextDescriptorUploadGeneration;
  const bool volatileUploadDirty =
      !descriptorUploadDirty &&
      current.volatileUploadGeneration != nextVolatileUploadGeneration;

  decision.rebuildFrameGraph = graphDirty;
  decision.rebuildRenderInputs = renderInputsDirty;
  decision.rebuildDescriptorUploadPlans = descriptorUploadDirty;
  decision.syncUploadPlans = descriptorUploadDirty;
  decision.syncVolatileResources = volatileUploadDirty;
  decision.touchCachedUploadResources = current.valid && !descriptorUploadDirty;
  decision.nextSnapshot = PreparedRenderStateCacheSnapshot{
      .valid = true,
      .key = nextKey,
      .descriptorResourceSelectionGeneration =
          nextDescriptorResourceSelectionGeneration,
      .descriptorUploadGeneration = nextDescriptorUploadGeneration,
      .volatileUploadGeneration = nextVolatileUploadGeneration,
  };
  return decision;
}

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

bool hasReadOnlyDepthAttachment(const LX_core::FramePass &graphPass) {
  return std::any_of(
      graphPass.attachments.begin(), graphPass.attachments.end(),
      [](const LX_core::RenderPathAttachmentContract &attachment) {
        return attachment.depth &&
               attachment.attachmentUsage ==
                   LX_core::RenderPathAttachmentUsage::DepthAttachmentReadOnly;
      });
}

void validateOffscreenWritesMatchTarget(
    const LX_core::CompiledFrameGraphPass &pass,
    const LX_core::FramePass &graphPass) {
  const auto colorWrites =
      findWritesForKind(pass, LX_core::FrameGraphAttachmentKind::Color);
  const auto depthWrite =
      findWriteForKind(pass, LX_core::FrameGraphAttachmentKind::Depth);

  if (pass.target.colorAttachmentCount() != colorWrites.size()) {
    throw std::runtime_error(
        "Frame graph offscreen pass color write does not match target: " +
        LX_core::GlobalStringTable::get().getName(pass.name.id));
  }
  if (pass.target.depthFormat.has_value() &&
      !depthWrite.has_value() && hasReadOnlyDepthAttachment(graphPass)) {
    return;
  }
  if (pass.target.depthFormat.has_value() != depthWrite.has_value()) {
    throw std::runtime_error(
        "Frame graph offscreen pass depth write does not match target: " +
        LX_core::GlobalStringTable::get().getName(pass.name.id));
  }
}

bool liveRenderViewSelectionChanged(
    const std::optional<LX_core::gpu::LiveRenderView> &lhs,
    const std::optional<LX_core::gpu::LiveRenderView> &rhs) {
  if (lhs.has_value() != rhs.has_value()) {
    return true;
  }
  if (!lhs.has_value()) {
    return false;
  }
  return lhs->visibleMask != rhs->visibleMask ||
         lhs->realtimeRenderPathGraph != rhs->realtimeRenderPathGraph ||
         lhs->runtimeExtents != rhs->runtimeExtents ||
         lhs->previewEnabled != rhs->previewEnabled ||
         lhs->editorOverlayVisible != rhs->editorOverlayVisible;
}

bool liveRenderViewRenderPathGraphChanged(
    const std::optional<LX_core::gpu::LiveRenderView> &lhs,
    const std::optional<LX_core::gpu::LiveRenderView> &rhs) {
  const auto graph = [](const auto &view) -> std::string_view {
    return view.has_value() ? std::string_view(view->realtimeRenderPathGraph)
                            : std::string_view{};
  };
  return graph(lhs) != graph(rhs);
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
    m_swapchainImageLayouts.assign(m_swapchain->getImageCount(),
                                   VK_IMAGE_LAYOUT_UNDEFINED);

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
    guiParams.renderPass = VK_NULL_HANDLE;
    guiParams.useDynamicRendering = true;
    guiParams.colorAttachmentFormat = device().getSurfaceFormat().format;
    guiParams.swapchainImageCount = m_swapchain->getImageCount();
    m_gui.init(guiParams);
  }
  void shutdown() { destroy(); }
  void setPostProcessSettings(const VulkanPostProcessSettings &settings) {
    m_postProcessSettings = settings;
    if (m_scene) {
      initScene(m_scene);
    } else {
      invalidatePreparedRenderState();
    }
  }
  void setLiveRenderView(std::optional<LX_core::gpu::LiveRenderView> view) {
    const bool selectionChanged =
        liveRenderViewSelectionChanged(m_liveRenderView, view);
    const bool renderPathGraphChanged =
        liveRenderViewRenderPathGraphChanged(m_liveRenderView, view);
    m_liveRenderView = std::move(view);
    if (selectionChanged) {
      m_liveRenderViewSelectionGeneration =
          m_liveRenderViewSelectionGeneration == u64_max
              ? 1
              : m_liveRenderViewSelectionGeneration + 1;
    }
    if (m_scene && m_liveRenderView.has_value()) {
      (void)m_scene->resources().updateLiveRenderCameraUboResource(
          m_liveRenderView->cameraResource);
    }
    if (m_scene && renderPathGraphChanged) {
      initScene(m_scene);
    }
  }
  [[nodiscard]] LX_core::gpu::LiveRenderSubmissionStats
  liveRenderSubmissionStats() const {
    return m_lastLiveStats;
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

  void syncRenderUploadPlan(const LX_core::RenderUploadPlan &uploadPlan) {
    for (const auto &resource : uploadPlan.resources) {
      resourceManager().syncResource(commandBufferManager(), resource);
    }
  }

  [[nodiscard]] bool uploadPlanRequiresSharedHostBufferSync(
      const LX_core::RenderUploadPlan &uploadPlan) const {
    for (const auto &resource : uploadPlan.resources) {
      if (isDirtyHostBufferResource(resource)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static bool
  isBindlessSceneDescriptor(const LX_core::DescriptorResourceRef &descriptor) {
    const LX_core::StringID name = descriptor.getBindingName();
    return name == LX_core::StringID("SceneObjects") ||
           name == LX_core::StringID("SceneDraws") ||
           name == LX_core::StringID("SceneMaterials") ||
           name == LX_core::StringID("SceneMaterialRefs") ||
           name == LX_core::StringID("SceneSourceMaterialRecords") ||
           name == LX_core::StringID("SceneTextures");
  }

  void recordLiveDescStats(const LX_core::RenderInputDesc &desc) {
    ++m_currentLiveStats.compilerInputCount;
    if (desc.accepted()) {
      ++m_currentLiveStats.acceptedInputCount;
      m_currentLiveStats.submittedDrawCount += desc.stats.submittedDrawCount;
      m_currentLiveStats.submittedDispatchCount +=
          desc.stats.submittedDispatchCount;
      m_currentLiveStats.fallbackObservedCount +=
          desc.stats.fallbackObservedCount;
      for (const LX_core::DescriptorResourceRef &descriptor :
           desc.bindingPlan.descriptors) {
        if (isBindlessSceneDescriptor(descriptor)) {
          ++m_currentLiveStats.bindlessSceneDescriptorCount;
        }
      }
    } else {
      ++m_currentLiveStats.rejectedInputCount;
    }
  }

  void syncDescriptorResource(const LX_core::DescriptorResourceRef &resource) {
    if (resource.isTextureArray()) {
      for (const LX_core::TextureSamplerRef &texture : resource.textures()) {
        if (texture.isValid()) {
          resourceManager().syncResource(
              commandBufferManager(), LX_core::GpuResourceRef{texture.get()});
        }
      }
      return;
    }
    if (resource.resource().isValid()) {
      resourceManager().syncResource(commandBufferManager(),
                                     resource.resource());
    }
  }

  [[nodiscard]] bool descriptorResourceRequiresSharedHostBufferSync(
      const LX_core::DescriptorResourceRef &resource) const {
    if (resource.isTextureArray()) {
      for (const LX_core::TextureSamplerRef &texture : resource.textures()) {
        if (texture.isValid() &&
            isDirtyHostBufferResource(LX_core::GpuResourceRef{texture.get()})) {
          return true;
        }
      }
      return false;
    }
    return resource.resource().isValid() &&
           isDirtyHostBufferResource(resource.resource());
  }

  [[nodiscard]] LX_core::RenderWorkBuildContext
  makeRealtimeRenderWorkContext() const {
    if (!m_scene) {
      throw std::runtime_error("realtime render work requires a scene");
    }
    return LX_core::RenderWorkBuildContext::forScene(
        LX_core::RenderDomain::Realtime, *m_scene,
        makeRealtimeRenderWorkOptionsForCompiledPass(std::nullopt));
  }

  void appendDescriptorResources(
      LX_core::DescriptorResourceList &out,
      const LX_core::DescriptorResourceList &resources) const {
    out.insert(out.end(), resources.begin(), resources.end());
  }

  LX_core::RenderWorkBuildContext::PassPreparationFacts &
  ensurePassPreparationFacts(
      std::vector<LX_core::RenderWorkBuildContext::PassPreparationFacts> &facts,
      LX_core::StringID pass) const {
    const auto it = std::find_if(
        facts.begin(), facts.end(),
        [pass](const LX_core::RenderWorkBuildContext::PassPreparationFacts
                   &candidate) { return candidate.pass == pass; });
    if (it != facts.end()) {
      return *it;
    }
    LX_core::RenderWorkBuildContext::PassPreparationFacts next;
    next.pass = pass;
    facts.push_back(std::move(next));
    return facts.back();
  }

  [[nodiscard]] u32
  shadowCascadeIndexForCompiledPass(usize compiledPassIndex) const {
    u32 cascadeIndex = 0;
    const auto &passes = m_compiledFrameGraph.getPasses();
    for (usize i = 0; i < compiledPassIndex && i < passes.size(); ++i) {
      if (passes[i].name == LX_core::Pass_Shadow) {
        ++cascadeIndex;
      }
    }
    return cascadeIndex;
  }

  [[nodiscard]] LX_core::RenderWorkBuildContext::Options
  makeRealtimeRenderWorkOptionsForCompiledPass(
      std::optional<usize> compiledPassIndex) const {
    LX_core::RenderWorkBuildContext::Options options;
    if (m_liveRenderView.has_value()) {
      options.cameraResource = m_liveRenderView->cameraResource;
      options.visibleMask = m_liveRenderView->visibleMask;
      options.runtimeExtents.reserve(m_liveRenderView->runtimeExtents.size());
      for (const LX_core::gpu::LiveRenderRuntimeExtent &extent :
           m_liveRenderView->runtimeExtents) {
        options.runtimeExtents.push_back(
            LX_core::RenderWorkBuildContext::RuntimeExtent{
                .key = extent.key,
                .extent = extent.extent,
            });
      }
    }
    options.passPreparationFacts.reserve(m_basePassPreparationFacts.size() + 1);
    for (const auto &[_, facts] : m_basePassPreparationFacts) {
      options.passPreparationFacts.push_back(facts);
    }

    if (!compiledPassIndex.has_value() ||
        *compiledPassIndex >= m_compiledFrameGraph.getPasses().size()) {
      return options;
    }

    const auto &compiledPass =
        m_compiledFrameGraph.getPasses()[*compiledPassIndex];
    auto &facts = ensurePassPreparationFacts(options.passPreparationFacts,
                                             compiledPass.name);
    if (*compiledPassIndex < m_compiledPassDescriptorResources.size()) {
      appendDescriptorResources(
          facts.descriptorResources,
          m_compiledPassDescriptorResources[*compiledPassIndex]);
    }
    if (compiledPass.name == LX_core::Pass_Shadow) {
      const u32 cascadeIndex =
          shadowCascadeIndexForCompiledPass(*compiledPassIndex);
      if (cascadeIndex < m_shadowCascadeUboSnapshots.size() &&
          m_shadowCascadeUboSnapshots[cascadeIndex]) {
        facts.descriptorResources.emplace_back(
            *m_shadowCascadeUboSnapshots[cascadeIndex]);
      }
    }
    return options;
  }

  [[nodiscard]] LX_core::RenderWorkBuildContext
  makeRealtimeRenderWorkContextForCompiledPass(usize compiledPassIndex) const {
    if (!m_scene) {
      throw std::runtime_error("realtime render work requires a scene");
    }
    return LX_core::RenderWorkBuildContext::forScene(
        LX_core::RenderDomain::Realtime, *m_scene,
        makeRealtimeRenderWorkOptionsForCompiledPass(compiledPassIndex));
  }

  struct PreparedRenderPassInputs final {
    LX_core::PreparedFramePassWork work;
    LX_core::RenderUploadPlan uploadPlan;
    bool uploadPlanValid = false;
  };

  [[nodiscard]] PreparedRenderPassInputs
  prepareRenderPassInputs(const LX_core::FramePass &pass,
                          const LX_core::RenderWorkBuildContext &context) {
    LX_core::RenderWorkCompiler compiler;
    PreparedRenderPassInputs prepared;
    prepared.work.passName = pass.name;
    compiler.buildInputs(pass, context, prepared.work.inputs);
    ++m_preparedRenderWorkDiagnostics.renderInputBuildCount;
    prepared.work.descs = compiler.prepare(pass, context, prepared.work.inputs);
    ++m_preparedRenderWorkDiagnostics.renderInputPrepareCount;
    return prepared;
  }

  void rebuildPreparedRenderPassInputs() {
    m_preparedCompiledPassInputs.clear();
    m_preparedCompiledPassInputs.resize(m_compiledFrameGraph.getPasses().size());
    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
      const LX_core::CompiledFrameGraphPass &compiledPass =
          compiledPasses[passIndex];
      if (compiledPass.sourcePassIndex >= m_frameGraph.getPasses().size()) {
        continue;
      }
      const LX_core::FramePass &pass =
          m_frameGraph.getPasses()[compiledPass.sourcePassIndex];
      const LX_core::RenderWorkBuildContext context =
          makeRealtimeRenderWorkContextForCompiledPass(passIndex);
      m_preparedCompiledPassInputs[passIndex] =
          prepareRenderPassInputs(pass, context);
    }
  }

  void rebuildPreparedUploadPlans() {
    for (PreparedRenderPassInputs &prepared : m_preparedCompiledPassInputs) {
      prepared.uploadPlan = LX_core::buildRenderUploadPlan(
          prepared.work.inputs, prepared.work.descs);
      prepared.uploadPlanValid = true;
      ++m_preparedRenderWorkDiagnostics.descriptorUploadPlanBuildCount;
    }
  }

  [[nodiscard]] const PreparedRenderPassInputs *
  preparedInputsForCompiledPass(usize passIndex) const {
    if (passIndex >= m_preparedCompiledPassInputs.size()) {
      return nullptr;
    }
    return &m_preparedCompiledPassInputs[passIndex];
  }

  void recordDiagnostic(const LX_core::RenderInputDesc &desc) const {
    if (!expRendererDebugEnabled()) {
      return;
    }
    for (const LX_core::RenderInputDiagnostic &diagnostic : desc.diagnostics) {
      std::cerr
          << "[RendererDebug] RenderInput rejected pass="
          << LX_core::GlobalStringTable::get().toDebugString(diagnostic.pass)
          << " debugId="
          << LX_core::GlobalStringTable::get().toDebugString(diagnostic.debugId)
          << " message=" << diagnostic.message << std::endl;
    }
  }

  void syncPreparedFramePassUploadPlans() {
    for (const PreparedRenderPassInputs &prepared :
         m_preparedCompiledPassInputs) {
      if (!prepared.uploadPlanValid) {
        continue;
      }
      syncRenderUploadPlan(prepared.uploadPlan);
    }
    if (m_scene) {
      m_preparedDescriptorResourceSelectionGeneration =
          m_scene->resources().descriptorResourceSelectionGeneration();
      m_preparedDescriptorUploadGeneration =
          m_scene->resources().descriptorUploadGeneration();
      m_preparedVolatileUploadGeneration =
          m_scene->resources().volatileUploadGeneration();
    }
    ++m_preparedRenderWorkDiagnostics.uploadPlanSyncCount;
  }

  [[nodiscard]] bool
  preparedFramePassUploadPlansRequireSharedHostBufferSync() const {
    for (const PreparedRenderPassInputs &prepared :
         m_preparedCompiledPassInputs) {
      if (!prepared.uploadPlanValid) {
        continue;
      }
      if (uploadPlanRequiresSharedHostBufferSync(prepared.uploadPlan)) {
        return true;
      }
    }
    return false;
  }

  void syncDirtyVolatilePreparedResources() {
    std::unordered_set<LX_core::ResourceCacheIdentity> synced;
    bool syncedAny = false;
    for (const PreparedRenderPassInputs &prepared :
         m_preparedCompiledPassInputs) {
      if (!prepared.uploadPlanValid) {
        continue;
      }
      for (const LX_core::GpuResourceRef &resource :
           prepared.uploadPlan.resources) {
        if (!isDirtyHostBufferResource(resource)) {
          continue;
        }
        if (!synced.insert(resource.getBackendCacheIdentity()).second) {
          continue;
        }
        resourceManager().syncResource(commandBufferManager(), resource);
        syncedAny = true;
      }
    }
    if (syncedAny) {
      ++m_preparedRenderWorkDiagnostics.volatileUploadSyncCount;
    }
    if (m_scene) {
      m_preparedVolatileUploadGeneration =
          m_scene->resources().volatileUploadGeneration();
    }
  }

  void touchPreparedUploadResources() {
    std::unordered_set<LX_core::ResourceCacheIdentity> touched;
    bool touchedAny = false;
    for (const PreparedRenderPassInputs &prepared :
         m_preparedCompiledPassInputs) {
      if (!prepared.uploadPlanValid) {
        continue;
      }
      for (const LX_core::GpuResourceRef &resource :
           prepared.uploadPlan.resources) {
        if (!resource.isValid() ||
            resource.get().getType() == LX_core::ResourceType::Special) {
          continue;
        }
        if (!touched.insert(resource.getBackendCacheIdentity()).second) {
          continue;
        }
        resourceManager().touchResource(resource);
        touchedAny = true;
      }
    }
    if (touchedAny) {
      ++m_preparedRenderWorkDiagnostics.cachedUploadResourceTouchCount;
    }
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

  [[nodiscard]] PreparedRenderStateKey
  makePreparedRenderStateKey(LX_core::RenderTargetDesc target) const {
    if (!m_scene) {
      return PreparedRenderStateKey{.target = std::move(target)};
    }
    const LX_core::SceneResourceTable &resources = m_scene->resources();
    return PreparedRenderStateKey{
        .graphGeneration = resources.graphGeneration(),
        .resourceGeneration = resources.resourceGeneration(),
        .featureGeneration = resources.featureGeneration(),
        .sceneNodeGeneration = m_scene->runtimeNodeGeneration() +
                               m_liveRenderViewSelectionGeneration,
        .target = std::move(target),
    };
  }

  [[nodiscard]] PreparedRenderStateCacheSnapshot
  preparedRenderStateSnapshot() const {
    if (!m_preparedRenderStateKey.has_value() || !m_scene) {
      return {};
    }
    return PreparedRenderStateCacheSnapshot{
        .valid = true,
        .key = *m_preparedRenderStateKey,
        .descriptorResourceSelectionGeneration =
            m_preparedDescriptorResourceSelectionGeneration.value_or(
                m_scene->resources()
                    .descriptorResourceSelectionGeneration()),
        .descriptorUploadGeneration =
            m_preparedDescriptorUploadGeneration.value_or(
                m_scene->resources().descriptorUploadGeneration()),
        .volatileUploadGeneration =
            m_preparedVolatileUploadGeneration.value_or(
                m_scene->resources().volatileUploadGeneration()),
    };
  }

  [[nodiscard]] PreparedRenderStateCacheDecision
  evaluateCurrentPreparedRenderState() const {
    const LX_core::RenderTargetDesc target = makeSwapchainTarget().toDesc();
    const u64 descriptorResourceSelectionGeneration =
        m_scene ? m_scene->resources().descriptorResourceSelectionGeneration()
                : 0;
    const u64 descriptorUploadGeneration =
        m_scene ? m_scene->resources().descriptorUploadGeneration() : 0;
    const u64 volatileUploadGeneration =
        m_scene ? m_scene->resources().volatileUploadGeneration() : 0;
    return evaluatePreparedRenderStateCache(
        preparedRenderStateSnapshot(), makePreparedRenderStateKey(target),
        descriptorResourceSelectionGeneration, descriptorUploadGeneration,
        volatileUploadGeneration);
  }

  void invalidatePreparedRenderState() {
    m_preparedRenderStateKey.reset();
    m_preparedDescriptorResourceSelectionGeneration.reset();
    m_preparedDescriptorUploadGeneration.reset();
    m_preparedVolatileUploadGeneration.reset();
    m_preparedCompiledPassInputs.clear();
  }

  void syncPreparedWorkIfInputOrDescriptorGenerationDirty(
      bool waitForSharedHostBuffers) {
    if (!m_scene) {
      return;
    }
    const PreparedRenderStateCacheDecision decision =
        evaluateCurrentPreparedRenderState();
    if (decision.rebuildFrameGraph || (!decision.rebuildRenderInputs &&
                                       !decision.rebuildDescriptorUploadPlans)) {
      return;
    }
    if (decision.rebuildRenderInputs && m_swapchain) {
      m_swapchain->waitForAllFrames();
    }
    if (decision.rebuildRenderInputs) {
      rebuildPreparedRenderPassInputs();
    }
    if (decision.rebuildDescriptorUploadPlans) {
      rebuildPreparedUploadPlans();
    }
    if (waitForSharedHostBuffers && m_swapchain &&
        preparedFramePassUploadPlansRequireSharedHostBufferSync()) {
      m_swapchain->waitForAllFrames();
    }
    syncPreparedFramePassUploadPlans();
  }

  void initScene(SceneSharedPtr _scene) {
    ++m_initSceneCallCount;
    if (m_swapchain) {
      m_swapchain->waitForAllFrames();
    }
    m_scene = _scene;
    invalidatePreparedRenderState();

    const LX_core::RenderTarget swapchainTarget = makeSwapchainTarget();
    const auto swapchainDesc = swapchainTarget.toDesc();
    LX_core::RenderTargetDesc forwardHdrDesc;
    forwardHdrDesc.role = LX_core::RenderTargetRole::Offscreen;
    forwardHdrDesc.colorFormat = LX_core::ImageFormat::RGBA16Float;
    forwardHdrDesc.depthFormat = swapchainTarget.depthFormat;
    const auto shadowTarget =
        LX_core::RenderTargetDesc::offscreenDepth(swapchainTarget.depthFormat);

    updateDirectionalLightCascades();
    auto &forwardRenderPass = resourceManager().getRenderPass(forwardHdrDesc);
    forwardRenderPass.setClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    m_frameGraph = LX_core::FrameGraph{}; // Fresh graph on every initScene.
    m_basePassPreparationFacts.clear();
    m_compiledPassDescriptorResources.clear();
    m_scene->resources().beginRenderResourceScope();
    const bool deferredMode = m_scene->realtimeRenderSettings().mode ==
                              LX_core::SceneRealtimeRenderMode::Deferred;
    LX_core::RenderTargetDesc gbufferDesc =
        LX_core::RenderTargetDesc::offscreenColors(
            {LX_core::ImageFormat::RGBA16Float,
             LX_core::ImageFormat::RGBA16Float,
             LX_core::ImageFormat::RGBA16Float},
            swapchainTarget.depthFormat);
    const auto deferredLightingDesc = LX_core::RenderTargetDesc::offscreenColor(
        LX_core::ImageFormat::RGBA16Float);
    const auto bloomDesc = LX_core::RenderTargetDesc::offscreenColor(
        LX_core::ImageFormat::RGBA16Float);
    const auto assignGraphTarget = [&](LX_core::FramePass &pass) {
      const auto syncSwapchainAttachmentFormats =
          [&](LX_core::FramePass &swapchainPass) {
            const auto colorFormats = swapchainDesc.getColorFormats();
            usize colorIndex = 0;
            for (auto &attachment : swapchainPass.attachments) {
              if (attachment.depth) {
                if (swapchainDesc.depthFormat.has_value()) {
                  attachment.format = *swapchainDesc.depthFormat;
                }
                continue;
              }
              if (colorIndex < colorFormats.size()) {
                attachment.format = colorFormats[colorIndex++];
              }
            }
          };
      if (pass.name == LX_core::Pass_Forward) {
        pass.target = forwardHdrDesc;
      } else if (pass.name == LX_core::Pass_Deferred) {
        pass.target = gbufferDesc;
      } else if (pass.name == LX_core::Pass_DeferredLighting) {
        pass.target = swapchainDesc;
        syncSwapchainAttachmentFormats(pass);
      } else if (pass.name == LX_core::Pass_BloomThreshold ||
                 pass.name == LX_core::Pass_BloomBlurH ||
                 pass.name == LX_core::Pass_BloomBlurV) {
        pass.target = bloomDesc;
      } else if (pass.name == LX_core::Pass_Bloom) {
        pass.target = swapchainDesc;
        syncSwapchainAttachmentFormats(pass);
      } else if (pass.name == LX_core::Pass_PostProcess) {
        pass.target = swapchainDesc;
        syncSwapchainAttachmentFormats(pass);
      } else if (pass.name == LX_core::Pass_Shadow) {
        pass.target = shadowTarget;
      } else if (pass.name == LX_core::Pass_DebugOverlay) {
        pass.target = swapchainDesc;
        syncSwapchainAttachmentFormats(pass);
        pass.phase = LX_core::FrameGraphPhase::Debug;
      }
    };
    const auto expandGraphDeclaredShadowCascadePass =
        [&](const LX_core::FramePass &graphPass) {
          if (!m_scene->renderSettings().shadows) {
            return;
          }
          for (u32 cascadeIndex = 0; cascadeIndex < LX_core::MaxShadowCascades;
               ++cascadeIndex) {
            LX_core::FramePass cascadePass = graphPass;
            const auto shadowDepth =
                LX_core::FrameGraphResourceRef::depthAttachment(
                    LX_core::StringID("shadow.cascade" +
                                      std::to_string(cascadeIndex)));
            cascadePass.writes = {LX_core::FrameGraphWrite{
                shadowDepth, graphPass.writes.front().writeMode}};
            m_frameGraph.addPass(std::move(cascadePass));
          }
        };
    const auto addGraphDeclaredPass = [&](LX_core::FramePass pass) {
      assignGraphTarget(pass);
      if (pass.name == LX_core::Pass_Shadow) {
        expandGraphDeclaredShadowCascadePass(pass);
        return;
      }
      addGraphFullscreenShaderItem(pass);
      m_frameGraph.addPass(std::move(pass));
    };
    const auto assignRuntimeTargetsForValidation =
        [&](LX_core::FrameGraph graph) {
          for (LX_core::FramePass &pass : graph.getPasses()) {
            assignGraphTarget(pass);
          }
          return graph;
        };
    const auto validateRenderPathFeaturesOrThrow =
        [&](const LX_core::RenderPathGraph &graph,
            const LX_core::FrameGraph &frameGraph) {
          const auto diagnostics =
              LX_core::validateRenderPathFeatureCombination(
                  graph, frameGraph, m_scene->resources());
          std::vector<std::string> fatalDiagnostics;
          for (const auto &diagnostic : diagnostics) {
            if (!diagnostic.fatal) {
              continue;
            }
            std::cerr << diagnostic.message << '\n';
            fatalDiagnostics.push_back(diagnostic.message);
          }
          if (fatalDiagnostics.empty()) {
            return;
          }

          m_frameGraph = LX_core::FrameGraph{};
          m_compiledFrameGraph = LX_core::CompiledFrameGraph{};
          m_compiledPassDescriptorResources.clear();

          std::string message = "RenderPathFeature validation failed";
          for (const std::string &diagnostic : fatalDiagnostics) {
            message += "\n  ";
            message += diagnostic;
          }
          throw std::runtime_error(message);
        };
    if (deferredMode) {
      const bool hasViewSelectedGraph =
          m_liveRenderView.has_value() &&
          !m_liveRenderView->realtimeRenderPathGraph.empty();
      const std::string deferredGraphAsset =
          hasViewSelectedGraph
              ? m_liveRenderView->realtimeRenderPathGraph
              : (m_postProcessSettings.bloomEnabled
                     ? kDefaultDeferredBloomRenderPathGraphAsset
                     : kDefaultDeferredRenderPathGraphAsset);
      const LX_core::RenderPathGraph deferredRenderPathGraph =
          loadRenderPathGraphAsset(*m_scene, deferredGraphAsset,
                                   LX_core::RenderPath::Deferred);
      applyToneMappingFeatureSettings(m_scene->resources(),
                                      deferredRenderPathGraph,
                                      m_postProcessSettings);
      if (!hasViewSelectedGraph) {
        const std::vector<LX_core::StringID> deferredPasses =
            m_postProcessSettings.bloomEnabled
                ? std::vector<LX_core::StringID>{LX_core::Pass_Shadow,
                                                 LX_core::Pass_Deferred,
                                                 LX_core::Pass_DeferredLighting,
                                                 LX_core::Pass_DebugOverlay}
                : std::vector<LX_core::StringID>{
                      LX_core::Pass_Shadow, LX_core::Pass_Deferred,
                      LX_core::Pass_DeferredLighting,
                      LX_core::Pass_DebugOverlay};
        LX_core::validateRenderPathGraphPassSet(deferredRenderPathGraph,
                                                deferredPasses,
                                                deferredPasses);
      }
      resolveMaterialSourceVariantsOrThrow(
          *m_scene, deferredRenderPathGraph,
          LX_core::ResourceUri(deferredGraphAsset));
      LX_core::FrameGraph deferredGraph =
          LX_core::buildFrameGraphFromRenderPathGraph(
              deferredRenderPathGraph,
              LX_core::GraphResourceRegistry::makeDefault());
      deferredGraph = assignRuntimeTargetsForValidation(std::move(deferredGraph));
      validateRenderPathFeaturesOrThrow(deferredRenderPathGraph,
                                        deferredGraph);
      for (auto pass : deferredGraph.getPasses()) {
        addGraphDeclaredPass(std::move(pass));
      }
    } else {
      const bool hasViewSelectedGraph =
          m_liveRenderView.has_value() &&
          !m_liveRenderView->realtimeRenderPathGraph.empty();
      const std::string forwardGraphAsset =
          hasViewSelectedGraph
              ? m_liveRenderView->realtimeRenderPathGraph
              : (m_postProcessSettings.bloomEnabled
                     ? kDefaultForwardBloomRenderPathGraphAsset
                     : kDefaultForwardRenderPathGraphAsset);
      const LX_core::RenderPathGraph forwardRenderPathGraph =
          loadRenderPathGraphAsset(*m_scene, forwardGraphAsset,
                                   LX_core::RenderPath::Forward);
      applyToneMappingFeatureSettings(m_scene->resources(),
                                      forwardRenderPathGraph,
                                      m_postProcessSettings);
      if (!hasViewSelectedGraph) {
        const std::vector<LX_core::StringID> forwardPasses{
            LX_core::Pass_Shadow, LX_core::Pass_Forward, LX_core::Pass_Bloom,
            LX_core::Pass_DebugOverlay};
        LX_core::validateRenderPathGraphPassSet(forwardRenderPathGraph,
                                                forwardPasses, forwardPasses);
      }
      resolveMaterialSourceVariantsOrThrow(
          *m_scene, forwardRenderPathGraph,
          LX_core::ResourceUri(forwardGraphAsset));
      LX_core::FrameGraph forwardGraph =
          LX_core::buildFrameGraphFromRenderPathGraph(
              forwardRenderPathGraph,
              LX_core::GraphResourceRegistry::makeDefault());
      forwardGraph = assignRuntimeTargetsForValidation(std::move(forwardGraph));
      validateRenderPathFeaturesOrThrow(forwardRenderPathGraph, forwardGraph);
      for (auto pass : forwardGraph.getPasses()) {
        addGraphDeclaredPass(std::move(pass));
      }
    }
    // RenderPathGraph coverage audit:
    // - Forward, Deferred/DeferredLighting, Shadow, and DebugOverlay are graph
    //   asset managed. Tone mapping is feature-gated inside the default
    //   Forward/DeferredLighting shaders through common shader functions.
    // - Shadow keeps a temporary named dynamic expansion from the
    // graph-declared
    //   Shadow pass to per-cascade runtime instances until RenderPathGraph can
    //   express cascade fan-out directly.

    if (deferredMode) {
      addDeferredLightingItem(deferredLightingDesc);
    }
    rebuildShadowCascadeUboSnapshots();

    m_compiledFrameGraph = m_frameGraph.compile();
    ++m_preparedRenderWorkDiagnostics.frameGraphCompileCount;
    if (!m_compiledFrameGraph.isValid()) {
      throw std::runtime_error(m_compiledFrameGraph.errorText());
    }
    attachFrameGraphSampledResources();
    resetOffscreenFramebuffers();
    resourceManager().clearFrameGraphAttachments();

    rebuildPreparedRenderPassInputs();
    rebuildPreparedUploadPlans();
    m_preparedRenderStateKey = makePreparedRenderStateKey(swapchainDesc);
    m_preparedDescriptorResourceSelectionGeneration.reset();
    m_preparedDescriptorUploadGeneration.reset();
    m_preparedVolatileUploadGeneration.reset();
    syncPreparedFramePassUploadPlans();
    resourceManager().collectGarbage();

    // Explicit pipeline preparation happens only after scene resources,
    // material source variants, render inputs, upload resources, and final
    // shader reflection are ready. Future pipeline cache package loading
    // belongs inside this phase and must validate the same PipelineBuildDesc
    // identities.
    preparePipelinesForLoadedScene();
  }

  void uploadData() {
    updateDirectionalLightCascades();

    const u32 currentFrameIndex = m_frameIndex % kMaxFramesInFlight;
    resourceManager().beginFrame(currentFrameIndex);

    syncPreparedWorkIfInputOrDescriptorGenerationDirty(
        /*waitForSharedHostBuffers=*/true);
    if (m_scene) {
      m_scene->resources().refreshDirtyRealtimeScenePayloadResources();
    }

    if (m_swapchain &&
        preparedFramePassUploadPlansRequireSharedHostBufferSync()) {
      // These buffers are single shared allocations, not per-frame slices.
      // Wait until every in-flight frame that could still read them has
      // completed before overwriting their contents from the CPU.
      m_swapchain->waitForAllFrames();
    }
    // Narrow volatile-data hook: runtime camera/light/shadow UBOs may update
    // without changing graph/resource/feature generations. Sync only dirty
    // host buffers from the already-prepared upload plans; do not rebuild
    // structural render inputs for this path.
    syncDirtyVolatilePreparedResources();
    touchPreparedUploadResources();
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
    syncPreparedWorkIfInputOrDescriptorGenerationDirty(
        /*waitForSharedHostBuffers=*/true);

    const VkExtent2D extent = m_swapchain->getExtent();
    m_currentLiveStats = {};
    m_currentLiveStats.usedExplicitCamera = m_liveRenderView.has_value();

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

    const usize finalSwapchainPassIndex = findFinalSwapchainPassIndex();
    const usize finalSwapchainGroupStartIndex =
        findFinalSwapchainGroupStartIndex(finalSwapchainPassIndex);

    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
      const auto &compiledPass = compiledPasses[passIndex];
      prepareShadowCascadePass(passIndex);
      const bool isFinalSwapchainGroup =
          compiledPass.target.role == LX_core::RenderTargetRole::Swapchain &&
          finalSwapchainPassIndex != compiledPasses.size() &&
          passIndex >= finalSwapchainGroupStartIndex &&
          passIndex <= finalSwapchainPassIndex;
      const bool beginGuiFrame = isFinalSwapchainGroup &&
                                 passIndex == finalSwapchainGroupStartIndex &&
                                 !skipGuiFrame;
      const bool endGuiFrame = isFinalSwapchainGroup &&
                               passIndex == finalSwapchainPassIndex &&
                               !skipGuiFrame;
      const auto renderingMode = explicitRenderingModeFor(compiledPass);
      if (renderingMode == LX_core::RenderPathNodeRenderingMode::Dynamic) {
        recordDynamicPass(passIndex, imageIndex, extent, *cmd, beginGuiFrame,
                          endGuiFrame);
      } else {
        recordTraditionalPass(passIndex, currentFrameIndex, imageIndex, extent,
                              *cmd, beginGuiFrame, endGuiFrame);
      }
    }

    transitionSwapchainImage(imageIndex, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, *cmd);
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
    m_currentLiveStats.usedBindlessSceneDescriptors =
        m_currentLiveStats.bindlessSceneDescriptorCount > 0;
    m_lastLiveStats = m_currentLiveStats;
  }

  void setDrawUiCallback(std::function<void()> cb) {
    m_drawUiCallback = std::move(cb);
  }

  [[nodiscard]] usize cachedResourceCount() const {
    return m_foundation ? resourceManager().getCachedResourceCount() : 0;
  }

  [[nodiscard]] usize frameGraphItemCount() const {
    usize total = 0;
    for (const PreparedRenderPassInputs &prepared :
         m_preparedCompiledPassInputs) {
      total += prepared.work.inputs.size();
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

  [[nodiscard]] PreparedRenderWorkDiagnostics
  preparedRenderWorkDiagnostics() const {
    return m_preparedRenderWorkDiagnostics;
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
         sanitizeAttachmentName(attachmentName) + ".png"));
    if (requestedScreenPath.has_value()) {
      m_pendingScreenDump = PendingScreenDump{.path = *requestedScreenPath};
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
    std::vector<unsigned char> bgrPixels =
        makeBmpPixelsFromDump(attachment.format, width, height, mapped);
    readback->unmap();

    writeDebugImageFile(path, width, height, bgrPixels);

    return VulkanFrameGraphAttachmentDumpResult{
        .path = path,
        .screenPath = requestedScreenPath.value_or(std::filesystem::path{}),
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
    struct RendererStateRestore final {
      Impl &renderer;
      SceneSharedPtr scene;
      LX_core::FrameGraph frameGraph;
      LX_core::CompiledFrameGraph compiledFrameGraph;
      std::unordered_map<LX_core::StringID,
                         LX_core::RenderWorkBuildContext::PassPreparationFacts,
                         LX_core::StringID::Hash>
          basePassPreparationFacts;
      std::vector<LX_core::DescriptorResourceList>
          compiledPassDescriptorResources;
      std::optional<PreparedRenderStateKey> preparedRenderStateKey;
      std::optional<u64> preparedDescriptorResourceSelectionGeneration;
      std::optional<u64> preparedDescriptorUploadGeneration;
      std::optional<u64> preparedVolatileUploadGeneration;
      std::vector<PreparedRenderPassInputs> preparedCompiledPassInputs;
      PreparedRenderWorkDiagnostics preparedRenderWorkDiagnostics;
      std::vector<std::vector<std::unique_ptr<VulkanFrameBuffer>>>
          offscreenFramebuffers;
      std::optional<LX_core::gpu::LiveRenderView> liveRenderView;
      LX_core::gpu::LiveRenderSubmissionStats currentLiveStats;
      LX_core::gpu::LiveRenderSubmissionStats lastLiveStats;
      ~RendererStateRestore() {
        renderer.device().waitIdle();
        renderer.m_scene = std::move(scene);
        renderer.m_frameGraph = std::move(frameGraph);
        renderer.m_compiledFrameGraph = std::move(compiledFrameGraph);
        renderer.m_basePassPreparationFacts =
            std::move(basePassPreparationFacts);
        renderer.m_compiledPassDescriptorResources =
            std::move(compiledPassDescriptorResources);
        renderer.m_preparedRenderStateKey = std::move(preparedRenderStateKey);
        renderer.m_preparedDescriptorResourceSelectionGeneration =
            std::move(preparedDescriptorResourceSelectionGeneration);
        renderer.m_preparedDescriptorUploadGeneration =
            std::move(preparedDescriptorUploadGeneration);
        renderer.m_preparedVolatileUploadGeneration =
            std::move(preparedVolatileUploadGeneration);
        renderer.m_preparedCompiledPassInputs =
            std::move(preparedCompiledPassInputs);
        renderer.m_preparedRenderWorkDiagnostics =
            preparedRenderWorkDiagnostics;
        renderer.m_offscreenFramebuffers = std::move(offscreenFramebuffers);
        renderer.m_liveRenderView = std::move(liveRenderView);
        renderer.m_currentLiveStats = currentLiveStats;
        renderer.m_lastLiveStats = lastLiveStats;
        renderer.resourceManager().clearFrameGraphAttachments();
        renderer.updateDirectionalLightCascades();
      }
    } stateRestore{.renderer = *this,
                   .scene = previousScene,
                   .frameGraph = std::move(m_frameGraph),
                   .compiledFrameGraph = std::move(m_compiledFrameGraph),
                   .basePassPreparationFacts =
                       std::move(m_basePassPreparationFacts),
                   .compiledPassDescriptorResources =
                       std::move(m_compiledPassDescriptorResources),
                   .preparedRenderStateKey =
                       std::move(m_preparedRenderStateKey),
                   .preparedDescriptorResourceSelectionGeneration =
                       std::move(
                           m_preparedDescriptorResourceSelectionGeneration),
                   .preparedDescriptorUploadGeneration =
                       std::move(m_preparedDescriptorUploadGeneration),
                   .preparedVolatileUploadGeneration =
                       std::move(m_preparedVolatileUploadGeneration),
                   .preparedCompiledPassInputs =
                       std::move(m_preparedCompiledPassInputs),
                   .preparedRenderWorkDiagnostics =
                       m_preparedRenderWorkDiagnostics,
                   .offscreenFramebuffers = std::move(m_offscreenFramebuffers),
                   .liveRenderView = std::move(m_liveRenderView),
                   .currentLiveStats = m_currentLiveStats,
                   .lastLiveStats = m_lastLiveStats};
    m_scene = std::move(scene);

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
    outputCameraResource.view =
        LX_core::makeCameraViewMatrix(sourceCamera.pose);
    outputCameraResource.proj =
        LX_core::makeCameraProjectionMatrix(outputProjection);
    outputCameraResource.cullingMask = outputCullingMask;
    outputCameraResource.active = true;
    const auto outputCameraView = outputCameraResource.view;
    const auto outputCameraProj = outputCameraResource.proj;

    auto sceneResources = m_scene->getSceneLevelResources(LX_core::Pass_Forward,
                                                          outputCameraResource);
    RealtimeProfileDebugInfo debugInfo;
    debugInfo.profileAspect = profileAspect;
    debugInfo.cameraAspect = outputProjection.aspect;
    debugInfo.cameraView = outputCameraView;
    debugInfo.cameraProj = outputCameraProj;
    debugInfo.cameraResourceCount = countCameraResources(sceneResources);
    debugInfo.lightResourceCount = countLightResources(sceneResources);
    debugInfo.lightDirection = findLightDirection(sceneResources);

    const VkExtent2D extent{output.width, output.height};
    m_liveRenderView = LX_core::gpu::LiveRenderView{
        .cameraResource = outputCameraResource,
        .visibleMask = outputCullingMask & ~LX_core::Layer_EditorOverlay,
        .realtimeRenderPathGraph = output.renderPathGraph.string(),
        .runtimeExtents =
            {LX_core::gpu::LiveRenderRuntimeExtent{
                .key = LX_core::StringID("offline.output.resolution"),
                .extent = LX_core::Vec3u{output.width, output.height, 1u},
            }},
    };
    initScene(m_scene);

    const LX_core::RenderTarget swapchainLikeTarget = makeSwapchainTarget();
    const auto makeProfileTargetForPass =
        [&](const LX_core::FramePass &pass) {
          const bool usesDepth = std::any_of(
              pass.attachments.begin(), pass.attachments.end(),
              [](const LX_core::RenderPathAttachmentContract &attachment) {
                return attachment.depth;
              });
          return LX_core::RenderTargetDesc::offscreenColors(
              {swapchainLikeTarget.colorFormat},
              usesDepth ? std::optional<LX_core::ImageFormat>{
                              swapchainLikeTarget.depthFormat}
                        : std::nullopt);
        };
    for (LX_core::FramePass &pass : m_frameGraph.getPasses()) {
      if (pass.target.role != LX_core::RenderTargetRole::Swapchain) {
        continue;
      }
      pass.target = makeProfileTargetForPass(pass);
      LX_core::syncFramePassAttachmentContractsWithTarget(pass);
    }
    m_compiledFrameGraph = m_frameGraph.compile();
    ++m_preparedRenderWorkDiagnostics.frameGraphCompileCount;
    if (!m_compiledFrameGraph.isValid()) {
      throw std::runtime_error(m_compiledFrameGraph.errorText());
    }
    attachFrameGraphSampledResources();
    resetOffscreenFramebuffers();
    resourceManager().clearFrameGraphAttachments();
    rebuildPreparedRenderPassInputs();
    rebuildPreparedUploadPlans();
    m_preparedRenderStateKey.reset();
    m_preparedDescriptorResourceSelectionGeneration.reset();
    m_preparedDescriptorUploadGeneration.reset();
    m_preparedVolatileUploadGeneration.reset();
    syncPreparedFramePassUploadPlans();

    for (usize passIndex = 0;
         passIndex < m_compiledFrameGraph.getPasses().size(); ++passIndex) {
      const auto &compiledPass = m_compiledFrameGraph.getPasses()[passIndex];
      if (compiledPass.sourcePassIndex >= m_frameGraph.getPasses().size()) {
        continue;
      }
      const LX_core::FramePass &pass =
          m_frameGraph.getPasses()[compiledPass.sourcePassIndex];
      const PreparedRenderPassInputs *prepared =
          preparedInputsForCompiledPass(passIndex);
      if (prepared == nullptr) {
        continue;
      }
      debugInfo.drawItemCount +=
          static_cast<u32>(prepared->work.inputs.size());
      for (const LX_core::RenderInputDesc &desc : prepared->work.descs) {
        if (desc.accepted()) {
          debugInfo.pipelineIdentity.push_back(
              makePipelineIdentityDebug(pass, desc));
        }
      }
    }

    LX_core::StringID outputAttachmentName("swapchain.color");
    VkFormat colorFormat = toVkFormat(swapchainLikeTarget.colorFormat);
    for (auto passIt = m_frameGraph.getPasses().rbegin();
         passIt != m_frameGraph.getPasses().rend(); ++passIt) {
      bool foundReadback = false;
      for (auto readbackIt = passIt->readbacks.rbegin();
           readbackIt != passIt->readbacks.rend(); ++readbackIt) {
        if (readbackIt->kind != LX_core::RenderPathOutputKind::Image2D ||
            readbackIt->target.empty()) {
          continue;
        }
        outputAttachmentName = LX_core::StringID(readbackIt->target);
        const auto attachmentIt = std::find_if(
            passIt->attachments.begin(), passIt->attachments.end(),
            [&](const LX_core::RenderPathAttachmentContract &attachment) {
              return !attachment.depth && attachment.target == readbackIt->target;
            });
        if (attachmentIt == passIt->attachments.end()) {
          throw std::runtime_error(
              "realtime profile readback target attachment is missing: " +
              readbackIt->target);
        }
        colorFormat = toVkFormat(attachmentIt->format);
        foundReadback = true;
        break;
      }
      if (foundReadback) {
        break;
      }
    }
    const auto colorRef =
        LX_core::FrameGraphResourceRef::colorAttachment(outputAttachmentName);
    const VkDeviceSize byteSize =
        dumpByteSize(colorFormat, output.width, output.height);
    auto readback = VulkanBuffer::create(
        device(), byteSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    device().waitIdle();
    m_currentLiveStats = {};
    resourceManager().clearFrameGraphAttachments();
    auto cmd = commandBufferManager().beginSingleTimeCommands();
    debugInfo.viewportExtent = extent;
    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
      prepareShadowCascadePass(passIndex);
      const auto renderingMode = explicitRenderingModeFor(
          compiledPasses[passIndex]);
      if (renderingMode == LX_core::RenderPathNodeRenderingMode::Dynamic) {
        recordDynamicPass(passIndex, 0u, extent, *cmd,
                          /*beginGuiFrame=*/false,
                          /*endGuiFrame=*/false);
      } else {
        recordTraditionalPass(passIndex, 0u, 0u, extent, *cmd,
                              /*beginGuiFrame=*/false,
                              /*endGuiFrame=*/false);
      }
    }

    auto colorAttachment = resourceManager().getFrameGraphAttachment(
        colorRef.name);
    if (!colorAttachment.has_value() || !colorAttachment->get().texture) {
      throw std::runtime_error(
          "realtime profile output did not produce requested color attachment");
    }
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
                           colorAttachment->get().texture->getHandle(),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback->getHandle(), 1, &region);
    commandBufferManager().endSingleTimeCommands(std::move(cmd),
                                                 device().getGraphicsQueue());

    const void *mapped = readback->map();
    std::vector<unsigned char> rgba;
    std::optional<LX_core::offline::OfflineReadbackImage> linearImage;
    if (colorFormat == VK_FORMAT_R16G16B16A16_SFLOAT ||
        colorFormat == VK_FORMAT_R32G32B32A32_SFLOAT) {
      linearImage = makeRgba32fImageFromDump(colorFormat, output.width,
                                             output.height, mapped);
    } else {
      rgba = makeRgbaPixelsFromDump(colorFormat, output.width, output.height,
                                    mapped);
    }
    readback->unmap();

    const std::filesystem::path outputDir = basePath.parent_path();
    std::filesystem::create_directories(outputDir);
    const std::string outputStem = basePath.filename().empty()
                                       ? std::string("render")
                                       : basePath.filename().generic_string();
    VulkanRealtimeProfileOutputResult result{
        .linearExrPath = {},
        .cpuSrgbPngPath = outputDir / (outputStem + "-realtime.png"),
        .pipelineSrgbPngPath = outputDir / (outputStem + "-realtime.png"),
        .depthDebugPath = {},
        .metadataPath = outputDir / (outputStem + ".json"),
        .renderInputStats = toRealtimeProfileInputStats(m_currentLiveStats),
        .width = output.width,
        .height = output.height,
    };
    if (linearImage.has_value()) {
      LX_infra::image::writeToneMappedPng(result.cpuSrgbPngPath, *linearImage,
                                          LX_core::image::ToneMappingSettings{});
    } else {
      LX_infra::image::writeRawRgba8Png(result.cpuSrgbPngPath, output.width,
                                        output.height, rgba);
    }
    writeRealtimeProfileMetadata(
        result.metadataPath, result,
        "available: realtime profile graph readback", debugInfo);
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

    // The final contiguous swapchain run owns one ImGui frame. Dynamic
    // rendering still records one begin/end per pass; this boundary only
    // decides where ImGui::NewFrame and ImGui::Render happen.
    return start;
  }

  void drawPassQueue(usize passIndex, VulkanCommandBuffer &cmd) {
    if (passIndex >= m_compiledFrameGraph.getPasses().size()) {
      return;
    }
    const LX_core::CompiledFrameGraphPass &compiledPass =
        m_compiledFrameGraph.getPasses()[passIndex];
    if (compiledPass.sourcePassIndex >= m_frameGraph.getPasses().size()) {
      return;
    }

    const PreparedRenderPassInputs *prepared =
        preparedInputsForCompiledPass(passIndex);
    if (prepared == nullptr) {
      return;
    }
    const detail::VulkanPreparedFramePassRecordStats stats =
        detail::recordPreparedFramePassWork(
            VulkanFrameGraphExecutionTarget{.resourceManager =
                                                &resourceManager(),
                                            .commandBuffer = &cmd},
            compiledPass, prepared->work,
            detail::VulkanPreparedFramePassRecordHooks{
                .observeDesc =
                    [this](const LX_core::RenderInputDesc &desc) {
                      recordLiveDescStats(desc);
                    },
                .observeRejectedDesc =
                    [this](const LX_core::RenderInputDesc &desc) {
                      recordDiagnostic(desc);
                    },
            });
    m_currentLiveStats.descPipelineLookupCount += stats.pipelineLookupCount;
    m_currentLiveStats.descBoundInputCount += stats.boundInputCount;
    m_currentLiveStats.descExecutedInputCount += stats.executedInputCount;
  }

  void addFullscreenMaterialItem(LX_core::StringID pass,
                                 const LX_core::RenderTargetDesc &target,
                                 LX_core::MaterialInstance::UniquePtr material,
                                 const char *objectSignature) {
    (void)target;
    (void)objectSignature;
    if (!m_scene || !material) {
      return;
    }
    LX_core::RenderWorkBuildContext::PassPreparationFacts facts;
    facts.pass = pass;
    facts.shaderInfo = material->getPassShader(pass);
    facts.renderState = material->getPassRenderState(pass);
    const auto shaderProgram = material->getPassShaderProgram(pass);
    if (!shaderProgram.has_value()) {
      throw std::logic_error(
          "fullscreen material facts missing shader program for pass " +
          LX_core::GlobalStringTable::get().toDebugString(pass));
    }
    facts.shaderProgram = shaderProgram->get();
    facts.pipelineVariantKey =
        material->getMaterialTypeVariantSignature(shaderProgram->get());
    const LX_core::MaterialHandle materialHandle =
        m_scene->resources().addRenderMaterial(std::move(material));
    facts.descriptorResources = LX_core::buildSceneMaterialDescriptorResources(
        m_scene->resources(), materialHandle, facts.shaderInfo);
    m_basePassPreparationFacts[pass] = std::move(facts);
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
    const auto colorFormats = target.getColorFormats();
    const bool targetIsSrgb = !colorFormats.empty() &&
                              LX_core::isSrgbImageFormat(colorFormats.front());
    const VulkanPostProcessOutputEncoding outputEncoding =
        targetIsSrgb ? VulkanPostProcessOutputEncoding::Linear
                     : VulkanPostProcessOutputEncoding::Srgb;
    addFullscreenMaterialItem(
        LX_core::Pass_PostProcess, target,
        builder.createStandardPostProcessMaterial(outputEncoding),
        "PostProcessFullscreenTriangle");
  }

  void addDeferredLightingItem(const LX_core::RenderTargetDesc &target) {
    VulkanPostProcessBuilder builder(m_postProcessSettings);
    auto material = builder.createDeferredLightingMaterial();
    if (!m_scene || !material) {
      return;
    }
    addFullscreenMaterialItem(LX_core::Pass_DeferredLighting, target,
                              std::move(material),
                              "DeferredLightingFullscreenTriangle");
    auto factsIt =
        m_basePassPreparationFacts.find(LX_core::Pass_DeferredLighting);
    if (factsIt == m_basePassPreparationFacts.end()) {
      return;
    }
    const LX_core::RenderTarget defaultCameraTarget{};
    auto sceneResources = m_scene->getSceneLevelResources(
        LX_core::Pass_DeferredLighting, defaultCameraTarget);
    appendDescriptorResources(factsIt->second.descriptorResources,
                              sceneResources);
  }

  void addGraphFullscreenShaderItem(const LX_core::FramePass &pass) {
    if (!m_scene ||
        pass.input.kind != LX_core::RenderPassInputKind::FullscreenTriangle) {
      return;
    }
    LX_core::RenderWorkBuildContext::PassPreparationFacts facts;
    facts.pass = pass.name;
    facts.shaderInfo = loadRuntimeGraphicsShaderPayload(pass.shaderUri);
    facts.shaderProgram.shaderName = pass.shaderUri.string();
    facts.shaderProgram.shader = facts.shaderInfo;
    facts.pipelineVariantKey =
        LX_core::StringID("graph-fullscreen:" + pass.shaderUri.string());
    facts.renderState = pass.renderState;
    m_basePassPreparationFacts[pass.name] = std::move(facts);
  }

  LX_core::DirectionalLight *mainDirectionalLight() const {
    if (!m_scene) {
      return nullptr;
    }
    for (const auto &light : m_scene->getLights()) {
      const auto lightNode = light.get().getSceneNode();
      if (!lightNode) {
        continue;
      }
      const auto directional = m_scene->getDirectionalLight(*lightNode);
      if (directional.has_value() &&
          directional->get().supportsPass(LX_core::Pass_Shadow)) {
        return &directional->get();
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
      resourceManager().syncResource(
          commandBufferManager(), *m_shadowCascadeUboSnapshots[cascadeIndex]);
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

  void preparePipelinesForLoadedScene() {
    std::unordered_set<LX_core::PipelineKey, LX_core::PipelineKey::Hash>
        seenPipelines;
    std::vector<LX_core::PipelineBuildDesc> pipelineDescs;
    const auto appendPipelineDesc = [&](LX_core::PipelineBuildDesc desc) {
      if (seenPipelines.insert(desc.key).second) {
        pipelineDescs.push_back(std::move(desc));
      }
    };
    for (const PreparedRenderPassInputs &prepared :
         m_preparedCompiledPassInputs) {
      for (const LX_core::RenderInputDesc &desc : prepared.work.descs) {
        if (desc.accepted()) {
          appendPipelineDesc(desc.pipelineBuildDesc);
        }
      }
    }
    resourceManager().preloadPipelines(pipelineDescs);
  }

  void attachFrameGraphSampledResources() {
    m_compiledPassDescriptorResources.clear();
    m_compiledPassDescriptorResources.resize(
        m_compiledFrameGraph.getPasses().size());

    const auto appendReadToCompiledPass =
        [this](usize compiledPassIndex, const LX_core::FrameGraphRead &read) {
          if (read.bindingName == LX_core::StringID{}) {
            return;
          }
          auto resource = std::make_unique<LX_core::FrameGraphSampledResource>(
              read.resource, read.bindingName);
          const auto resourceRef =
              m_scene->resources().addRenderGpuResource(std::move(resource));
          m_compiledPassDescriptorResources[compiledPassIndex].emplace_back(
              resourceRef.get());
        };

    const auto &compiledPasses = m_compiledFrameGraph.getPasses();
    const auto &graphPasses = m_frameGraph.getPasses();
    for (usize passIndex = 0; passIndex < compiledPasses.size(); ++passIndex) {
      const auto &compiledPass = compiledPasses[passIndex];
      if (compiledPass.sourcePassIndex >= graphPasses.size()) {
        continue;
      }
      bool hasBloomColor = false;
      std::optional<LX_core::FrameGraphRead> sceneColorRead;
      for (const auto &read : compiledPass.reads) {
        if (read.bindingName == LX_core::StringID("BloomColor")) {
          hasBloomColor = true;
        }
        if (compiledPass.name == LX_core::Pass_PostProcess &&
            read.bindingName == LX_core::StringID("SceneColor")) {
          sceneColorRead = read;
        }
        appendReadToCompiledPass(passIndex, read);
      }
      if (compiledPass.name == LX_core::Pass_PostProcess && !hasBloomColor &&
          sceneColorRead.has_value()) {
        LX_core::FrameGraphRead bloomFallback = *sceneColorRead;
        bloomFallback.bindingName = LX_core::StringID("BloomColor");
        appendReadToCompiledPass(passIndex, bloomFallback);
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
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
      srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
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
                                  VulkanCommandBuffer &cmd,
                                  bool createFramebuffer = true) {
    if (pass.target.role == LX_core::RenderTargetRole::Swapchain) {
      return fallbackExtent;
    }

    const LX_core::FramePass &graphPass = sourceGraphPassFor(pass);
    validateOffscreenWritesMatchTarget(pass, graphPass);

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
      if (write.has_value()) {
        appendAttachment(write->get(), *pass.target.depthFormat);
      } else {
        const auto depthAttachmentIt = std::find_if(
            graphPass.attachments.begin(), graphPass.attachments.end(),
            [](const LX_core::RenderPathAttachmentContract &attachment) {
              return attachment.depth &&
                     attachment.attachmentUsage ==
                         LX_core::RenderPathAttachmentUsage::
                             DepthAttachmentReadOnly;
            });
        if (depthAttachmentIt != graphPass.attachments.end()) {
          auto attachment = resourceManager().getFrameGraphAttachment(
              LX_core::StringID(depthAttachmentIt->target));
          if (!attachment.has_value() || !attachment->get().texture) {
            throw std::runtime_error(
                "Dynamic offscreen pass missing read-only depth attachment: " +
                depthAttachmentIt->target);
          }
          transitionFrameGraphAttachment(
              LX_core::FrameGraphResourceRef::depthAttachment(
                  LX_core::StringID(depthAttachmentIt->target)),
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, cmd);
          attachments.push_back(attachment->get().texture->getImageView());
        }
      }
    }

    if (createFramebuffer) {
      auto &framebuffer = m_offscreenFramebuffers[passIndex][currentFrameIndex];
      if (!framebuffer) {
        auto &renderPass = resourceManager().getRenderPass(pass.target);
        framebuffer = VulkanFrameBuffer::create(
            device(), renderPass.getHandle(), attachments, fallbackExtent);
      }
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

  const LX_core::FramePass &sourceGraphPassFor(
      const LX_core::CompiledFrameGraphPass &compiledPass) const {
    const auto &graphPasses = m_frameGraph.getPasses();
    if (compiledPass.sourcePassIndex >= graphPasses.size()) {
      throw std::runtime_error("CompiledFrameGraphPass has invalid source pass "
                               "index");
    }
    return graphPasses[compiledPass.sourcePassIndex];
  }

  LX_core::RenderPathNodeRenderingMode explicitRenderingModeFor(
      const LX_core::CompiledFrameGraphPass &compiledPass) const {
    const auto &graphPass = sourceGraphPassFor(compiledPass);
    if (!graphPass.renderingMode.has_value()) {
      throw std::runtime_error(
          "FrameGraph pass missing explicit RenderPathNode rendering.mode: " +
          LX_core::GlobalStringTable::get().toDebugString(graphPass.name));
    }
    return *graphPass.renderingMode;
  }

  u32 dynamicRenderingLayerCount(const LX_core::FramePass &graphPass) const {
    if (graphPass.attachments.empty()) {
      throw std::runtime_error(
          "Dynamic rendering pass has no attachment contract: " +
          LX_core::GlobalStringTable::get().toDebugString(graphPass.name));
    }
    const u32 layers = graphPass.attachments.front().layers;
    for (const auto &attachment : graphPass.attachments) {
      if (attachment.layers != layers) {
        throw std::runtime_error(
            "Dynamic rendering pass attachment layers mismatch: " +
            LX_core::GlobalStringTable::get().toDebugString(graphPass.name));
      }
    }
    return layers;
  }

  VkPipelineStageFlags swapchainStageForLayout(VkImageLayout layout) const {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_PIPELINE_STAGE_TRANSFER_BIT;
    default:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
  }

  VkAccessFlags swapchainAccessForLayout(VkImageLayout layout) const {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_ACCESS_TRANSFER_READ_BIT;
    default:
      return 0;
    }
  }

  void transitionSwapchainImage(u32 imageIndex, VkImageLayout newLayout,
                                VulkanCommandBuffer &cmd) {
    if (imageIndex >= m_swapchainImageLayouts.size()) {
      throw std::runtime_error("Swapchain image layout state is missing");
    }
    VkImageLayout &currentLayout = m_swapchainImageLayouts[imageIndex];
    if (currentLayout == newLayout) {
      return;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_swapchain->getImage(imageIndex);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = swapchainAccessForLayout(currentLayout);
    barrier.dstAccessMask = swapchainAccessForLayout(newLayout);

    cmd.pipelineBarrier(swapchainStageForLayout(currentLayout),
                        swapchainStageForLayout(newLayout), barrier);
    currentLayout = newLayout;
  }

  VkAttachmentLoadOp
  dynamicLoadOpForWrite(const LX_core::FrameGraphWrite &write) const {
    return write.writeMode.has_value() ? VK_ATTACHMENT_LOAD_OP_LOAD
                                       : VK_ATTACHMENT_LOAD_OP_CLEAR;
  }

  VkRenderingAttachmentInfo
  makeDynamicColorAttachmentInfo(VkImageView imageView,
                                 const LX_core::FrameGraphWrite &write) const {
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = imageView;
    info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.loadOp = dynamicLoadOpForWrite(write);
    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    info.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    return info;
  }

  VkRenderingAttachmentInfo
  makeDynamicDepthAttachmentInfo(VkImageView imageView,
                                 const LX_core::FrameGraphWrite *write,
                                 LX_core::RenderPathAttachmentUsage usage) const {
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = imageView;
    const bool readOnly =
        usage == LX_core::RenderPathAttachmentUsage::DepthAttachmentReadOnly;
    info.imageLayout = readOnly
                           ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    info.loadOp = readOnly ? VK_ATTACHMENT_LOAD_OP_LOAD
                           : dynamicLoadOpForWrite(*write);
    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    info.clearValue.depthStencil = {1.0f, 0};
    return info;
  }

  const LX_core::RenderPathAttachmentContract *
  dynamicDepthAttachmentContract(const LX_core::FramePass &graphPass) const {
    const auto it = std::find_if(
        graphPass.attachments.begin(), graphPass.attachments.end(),
        [](const LX_core::RenderPathAttachmentContract &attachment) {
          return attachment.depth;
        });
    return it == graphPass.attachments.end() ? nullptr : &*it;
  }

  void recordDynamicPass(usize passIndex, u32 imageIndex, VkExtent2D extent,
                         VulkanCommandBuffer &cmd, bool beginGuiFrame,
                         bool endGuiFrame) {
    const auto &compiledPass = m_compiledFrameGraph.getPasses()[passIndex];
    const auto &graphPass = sourceGraphPassFor(compiledPass);
    const VkExtent2D passExtent =
        compiledPass.target.role == LX_core::RenderTargetRole::Swapchain
            ? extent
            : prepareOffscreenPass(passIndex, m_frameIndex % kMaxFramesInFlight,
                                   compiledPass, extent, cmd,
                                   /*createFramebuffer=*/false);

    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    const auto colorWrites = findWritesForKind(
        compiledPass, LX_core::FrameGraphAttachmentKind::Color);
    colorAttachments.reserve(colorWrites.size());
    if (compiledPass.target.role == LX_core::RenderTargetRole::Swapchain) {
      transitionSwapchainImage(imageIndex,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, cmd);
      if (colorWrites.size() != 1) {
        throw std::runtime_error(
            "Dynamic swapchain pass must declare exactly one color write: " +
            LX_core::GlobalStringTable::get().toDebugString(compiledPass.name));
      }
      colorAttachments.push_back(makeDynamicColorAttachmentInfo(
          m_swapchain->getImageView(imageIndex), colorWrites.front().get()));
    } else {
      for (const auto &write : colorWrites) {
        auto attachment = resourceManager().getFrameGraphAttachment(
            write.get().resource.name);
        if (!attachment.has_value() || !attachment->get().texture) {
          throw std::runtime_error(
              "Dynamic offscreen pass missing color attachment: " +
              LX_core::GlobalStringTable::get().toDebugString(
                  write.get().resource.name));
        }
        colorAttachments.push_back(makeDynamicColorAttachmentInfo(
            attachment->get().texture->getImageView(), write.get()));
      }
    }

    std::optional<VkRenderingAttachmentInfo> depthAttachment;
    const LX_core::RenderPathAttachmentContract *depthContract =
        dynamicDepthAttachmentContract(graphPass);
    const auto depthWrite = findWriteForKind(
        compiledPass, LX_core::FrameGraphAttachmentKind::Depth);
    if (depthContract != nullptr) {
      const bool readOnly =
          depthContract->attachmentUsage ==
          LX_core::RenderPathAttachmentUsage::DepthAttachmentReadOnly;
      if (readOnly && graphPass.renderState.depthWriteEnable) {
        throw std::runtime_error(
            "Dynamic read-only depth attachment requires depthWrite=false: " +
            LX_core::GlobalStringTable::get().toDebugString(compiledPass.name));
      }
      if (compiledPass.target.role == LX_core::RenderTargetRole::Swapchain) {
        if (!depthWrite.has_value()) {
          throw std::runtime_error(
              "Dynamic swapchain read-only depth attachment is unsupported");
        }
        depthAttachment = makeDynamicDepthAttachmentInfo(
            m_swapchain->getDepthImageView(imageIndex), &depthWrite->get(),
            depthContract->attachmentUsage);
      } else {
        const LX_core::StringID depthName =
            depthWrite.has_value() ? depthWrite->get().resource.name
                                   : LX_core::StringID(depthContract->target);
        auto attachment = resourceManager().getFrameGraphAttachment(depthName);
        if (!attachment.has_value() || !attachment->get().texture) {
          throw std::runtime_error(
              "Dynamic offscreen pass missing depth attachment: " +
              LX_core::GlobalStringTable::get().toDebugString(depthName));
        }
        if (readOnly) {
          transitionFrameGraphAttachment(
              LX_core::FrameGraphResourceRef::depthAttachment(depthName),
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, cmd);
        }
        depthAttachment = makeDynamicDepthAttachmentInfo(
            attachment->get().texture->getImageView(),
            depthWrite.has_value() ? &depthWrite->get() : nullptr,
            depthContract->attachmentUsage);
      }
    }

    if (beginGuiFrame) {
      m_gui.beginFrame();
      if (m_drawUiCallback) {
        m_drawUiCallback();
      }
    }
    cmd.beginRendering(passExtent, colorAttachments,
                       depthAttachment.has_value() ? &*depthAttachment
                                                   : nullptr,
                       dynamicRenderingLayerCount(graphPass));
    cmd.setViewport(passExtent.width, passExtent.height);
    cmd.setScissor(passExtent.width, passExtent.height);
    drawPassQueue(passIndex, cmd);
    if (endGuiFrame) {
      m_gui.endFrame(cmd.getHandle());
    }
    cmd.endRendering();
    transitionPassWritesToShaderRead(compiledPass, cmd);
  }

  void recordTraditionalPass(usize passIndex, u32 currentFrameIndex,
                             u32 imageIndex, VkExtent2D extent,
                             VulkanCommandBuffer &cmd, bool beginGuiFrame,
                             bool endGuiFrame) {
    if (beginGuiFrame || endGuiFrame) {
      throw std::runtime_error(
          "GUI overlay requires dynamic rendering for swapchain passes");
    }
    const auto &compiledPass = m_compiledFrameGraph.getPasses()[passIndex];
    if (compiledPass.target.role == LX_core::RenderTargetRole::Swapchain) {
      auto &renderPass = resourceManager().getRenderPass();
      cmd.beginRenderPass(renderPass.getHandle(),
                          m_swapchain->getFramebuffer(imageIndex).getHandle(),
                          extent, renderPass.getClearValues());
      cmd.setViewport(extent.width, extent.height);
      cmd.setScissor(extent.width, extent.height);
      drawPassQueue(passIndex, cmd);
      cmd.endRenderPass();
      return;
    }

    const VkExtent2D passExtent =
        prepareOffscreenPass(passIndex, currentFrameIndex, compiledPass, extent,
                             cmd, /*createFramebuffer=*/true);
    auto &renderPass = resourceManager().getRenderPass(compiledPass.target);
    cmd.beginRenderPass(
        renderPass.getHandle(),
        m_offscreenFramebuffers[passIndex][currentFrameIndex]->getHandle(),
        passExtent, renderPass.getClearValues());
    cmd.setViewport(passExtent.width, passExtent.height);
    cmd.setScissor(passExtent.width, passExtent.height);
    drawPassQueue(passIndex, cmd);
    cmd.endRenderPass();
    transitionPassWritesToShaderRead(compiledPass, cmd);
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
    writeDebugImageFile(dump.path, dump.width, dump.height, bgrPixels);
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
    m_swapchainImageLayouts.assign(m_swapchain->getImageCount(),
                                   VK_IMAGE_LAYOUT_UNDEFINED);
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
  std::unordered_map<LX_core::StringID,
                     LX_core::RenderWorkBuildContext::PassPreparationFacts,
                     LX_core::StringID::Hash>
      m_basePassPreparationFacts{};
  std::vector<LX_core::DescriptorResourceList>
      m_compiledPassDescriptorResources{};
  std::optional<PreparedRenderStateKey> m_preparedRenderStateKey;
  std::optional<u64> m_preparedDescriptorResourceSelectionGeneration;
  std::optional<u64> m_preparedDescriptorUploadGeneration;
  std::optional<u64> m_preparedVolatileUploadGeneration;
  std::vector<PreparedRenderPassInputs> m_preparedCompiledPassInputs;
  PreparedRenderWorkDiagnostics m_preparedRenderWorkDiagnostics;
  std::vector<std::vector<std::unique_ptr<VulkanFrameBuffer>>>
      m_offscreenFramebuffers;
  u32 m_frameIndex = 0;
  usize m_initSceneCallCount = 0;
  bool m_swapchainNeedsRebuild = false;
  VulkanPostProcessSettings m_postProcessSettings{};
  infra::Gui m_gui{};
  std::function<void()> m_drawUiCallback{};
  std::optional<LX_core::gpu::LiveRenderView> m_liveRenderView;
  LX_core::gpu::LiveRenderSubmissionStats m_currentLiveStats;
  LX_core::gpu::LiveRenderSubmissionStats m_lastLiveStats;
  std::optional<PendingScreenDump> m_pendingScreenDump;
  u64 m_liveRenderViewSelectionGeneration = 0;
  std::vector<VkImageLayout> m_swapchainImageLayouts;
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

void VulkanRealtimeRenderer::setLiveRenderView(
    std::optional<gpu::LiveRenderView> view) {
  p_impl->setLiveRenderView(std::move(view));
}

gpu::LiveRenderSubmissionStats
VulkanRealtimeRenderer::liveRenderSubmissionStats() const {
  return p_impl->liveRenderSubmissionStats();
}

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

PreparedRenderWorkDiagnostics
VulkanRealtimeRenderer::preparedRenderWorkDiagnostics() const {
  return p_impl->preparedRenderWorkDiagnostics();
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

VulkanRealtimeProfileOutputResult
VulkanRealtimeRenderer::generateRealtimeProfileOutput(
    SceneSharedPtr scene, const LX_core::offline::OutputProfile &output,
    const std::filesystem::path &basePath) {
  return p_impl->generateRealtimeProfileOutput(std::move(scene), output,
                                               basePath);
}

VulkanDebugColorTransferExportResult
VulkanRealtimeRenderer::exportDebugColorTransfer(
    const VulkanDebugColorTransferExportRequest &) {
  throw std::runtime_error(
      "debug color transfer export is not available after the render-path "
      "shader layout migration; use live render-target dumps instead");
}

} // namespace LX_core::backend
