#pragma once

#include "core/rhi/image_format.hpp"
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace LX_core {

enum class RenderTargetRole : u8 {
  Swapchain,
  Offscreen,
};

enum class FrameGraphAttachmentKind : u8 {
  Color,
  Depth,
};

struct RenderTargetDesc {
  RenderTargetRole role = RenderTargetRole::Swapchain;
  std::optional<ImageFormat> colorFormat = ImageFormat::BGRA8;
  std::optional<ImageFormat> depthFormat = ImageFormat::D32Float;
  u8 sampleCount = 1;
  u32 layerCount = 1;

  static RenderTargetDesc swapchain(ImageFormat color, ImageFormat depth) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Swapchain;
    desc.colorFormat = color;
    desc.depthFormat = depth;
    return desc;
  }

  static RenderTargetDesc offscreenColor(ImageFormat color) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Offscreen;
    desc.colorFormat = color;
    desc.depthFormat = std::nullopt;
    return desc;
  }

  static RenderTargetDesc offscreenDepth(ImageFormat depth) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Offscreen;
    desc.colorFormat = std::nullopt;
    desc.depthFormat = depth;
    return desc;
  }

  bool operator==(const RenderTargetDesc &other) const {
    return role == other.role && colorFormat == other.colorFormat &&
           depthFormat == other.depthFormat &&
           sampleCount == other.sampleCount && layerCount == other.layerCount;
  }

  bool operator!=(const RenderTargetDesc &other) const {
    return !(*this == other);
  }

  usize getHash() const {
    usize hash = static_cast<usize>(role);
    const auto combine = [&hash](usize value) {
      hash ^= value + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
    };
    combine(colorFormat ? static_cast<usize>(*colorFormat) + 1u : 0u);
    combine(depthFormat ? static_cast<usize>(*depthFormat) + 1u : 0u);
    combine(static_cast<usize>(sampleCount));
    combine(static_cast<usize>(layerCount));
    return hash;
  }

  StringID getPipelineSignature() const {
    const auto roleText =
        role == RenderTargetRole::Swapchain ? "swapchain" : "offscreen";
    const auto formatText = [](const std::optional<ImageFormat> &format) {
      if (!format.has_value()) {
        return std::string{"none"};
      }
      return std::to_string(static_cast<u32>(*format));
    };
    return StringID("RenderTarget:role=" + std::string{roleText} +
                    ";color=" + formatText(colorFormat) +
                    ";depth=" + formatText(depthFormat) +
                    ";samples=" + std::to_string(sampleCount) +
                    ";layers=" + std::to_string(layerCount));
  }
};

/*
@source_analysis.section RenderTarget：旧 target 轴的兼容外壳
`FramePass` 已经用 `RenderTargetDesc` 保存完整 target 形状，但当前 scene/camera
筛选接口仍接收 `RenderTarget`。因此 `RenderTarget` 现在是兼容外壳：保留旧的
`colorFormat` / `depthFormat` / `sampleCount` 字段给既有调用点使用，同时额外保存
role、可空 attachment 和 layerCount，让 `matchesTarget` / `toDesc` 不会把
offscreen 或 depth-only target 误还原成默认 swapchain target。
*/
struct RenderTarget {
  RenderTargetRole role = RenderTargetRole::Swapchain;
  std::optional<ImageFormat> colorAttachmentFormat = ImageFormat::BGRA8;
  std::optional<ImageFormat> depthAttachmentFormat = ImageFormat::D32Float;
  ImageFormat colorFormat = ImageFormat::BGRA8;
  ImageFormat depthFormat = ImageFormat::D32Float;
  u8 sampleCount = 1;
  u32 layerCount = 1;

  RenderTarget() = default;
  RenderTarget(ImageFormat color, ImageFormat depth, u8 samples)
      : colorAttachmentFormat(color), depthAttachmentFormat(depth),
        colorFormat(color), depthFormat(depth), sampleCount(samples) {}
  RenderTarget(const RenderTargetDesc &desc)
      : role(desc.role), colorAttachmentFormat(desc.colorFormat),
        depthAttachmentFormat(desc.depthFormat),
        colorFormat(desc.colorFormat.value_or(ImageFormat::BGRA8)),
        depthFormat(desc.depthFormat.value_or(ImageFormat::D32Float)),
        sampleCount(desc.sampleCount), layerCount(desc.layerCount) {}

  RenderTargetDesc toDesc() const {
    RenderTargetDesc desc;
    desc.role = role;
    desc.colorFormat = colorAttachmentFormat;
    desc.depthFormat = depthAttachmentFormat;
    desc.sampleCount = sampleCount;
    desc.layerCount = layerCount;
    return desc;
  }

  operator RenderTargetDesc() const { return toDesc(); }

  bool operator==(const RenderTarget &other) const {
    return toDesc() == other.toDesc();
  }
  bool operator!=(const RenderTarget &other) const { return !(*this == other); }

  usize getHash() const { return toDesc().getHash(); }
};

/*
@source_analysis.section operator==：按完整描述比较 target 轴
`RenderTarget::operator==` 被 `Camera::matchesTarget` 用作 target 轴判定。比较
必须走 `toDesc()`，否则从 `RenderTargetDesc` 降级到兼容类型时会丢失
offscreen/depth-only 的 role、null attachment 和 layerCount 语义。
*/

} // namespace LX_core
