#pragma once

#include "core/rhi/image_format.hpp"
#include "core/platform/types.hpp"
#include "core/utils/string_table.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
  std::vector<ImageFormat> colorFormats;
  std::optional<ImageFormat> depthFormat = ImageFormat::D32Float;
  u8 sampleCount = 1;
  u32 layerCount = 1;

  static RenderTargetDesc swapchain(ImageFormat color, ImageFormat depth) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Swapchain;
    desc.colorFormat = color;
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    return desc;
  }

  static RenderTargetDesc offscreenColor(ImageFormat color) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Offscreen;
    desc.colorFormat = color;
    desc.colorFormats = {color};
    desc.depthFormat = std::nullopt;
    return desc;
  }

  static RenderTargetDesc offscreenColors(std::vector<ImageFormat> colors,
                                          std::optional<ImageFormat> depth) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Offscreen;
    desc.colorFormat =
        colors.empty() ? std::nullopt : std::optional<ImageFormat>{colors[0]};
    desc.colorFormats = std::move(colors);
    desc.depthFormat = depth;
    return desc;
  }

  static RenderTargetDesc offscreenDepth(ImageFormat depth) {
    RenderTargetDesc desc;
    desc.role = RenderTargetRole::Offscreen;
    desc.colorFormat = std::nullopt;
    desc.depthFormat = depth;
    return desc;
  }

  [[nodiscard]] std::vector<ImageFormat> getColorFormats() const {
    if (!colorFormats.empty()) {
      return colorFormats;
    }
    if (colorFormat.has_value()) {
      return {*colorFormat};
    }
    return {};
  }

  [[nodiscard]] usize colorAttachmentCount() const {
    return getColorFormats().size();
  }

  bool operator==(const RenderTargetDesc &other) const {
    return role == other.role && getColorFormats() == other.getColorFormats() &&
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
    const auto colors = getColorFormats();
    combine(colors.size());
    for (const auto format : colors) {
      combine(static_cast<usize>(format) + 1u);
    }
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
    std::string colorText;
    const auto colors = getColorFormats();
    if (colors.empty()) {
      colorText = "none";
    } else {
      for (usize i = 0; i < colors.size(); ++i) {
        if (i > 0u) {
          colorText += ",";
        }
        colorText += std::to_string(static_cast<u32>(colors[i]));
      }
    }
    return StringID("RenderTarget:role=" + std::string{roleText} +
                    ";colors=" + colorText +
                    ";depth=" + formatText(depthFormat) +
                    ";samples=" + std::to_string(sampleCount) +
                    ";layers=" + std::to_string(layerCount));
  }
};

/*
@source_analysis.section RenderTarget：旧 target 轴的兼容外壳
`FramePass` 已经用 `RenderTargetDesc` 保存完整 target 形状，但当前 scene/camera
筛选接口仍接收 `RenderTarget`。因此 `RenderTarget` 现在是兼容外壳：旧的
`colorFormat` / `depthFormat` / `sampleCount` 字段仍是 present attachment 的
格式来源，额外的 presence bit、role 和 layerCount 只补足旧结构表达不了的语义。
这样既有代码直接写 `target.colorFormat` / `target.depthFormat` 时不会被 `toDesc`
忽略，offscreen/depth-only target 也不会被误还原成默认 swapchain target。
*/
struct RenderTarget {
  RenderTargetRole role = RenderTargetRole::Swapchain;
  bool hasColorAttachment = true;
  bool hasDepthAttachment = true;
  ImageFormat colorFormat = ImageFormat::BGRA8;
  std::vector<ImageFormat> colorFormats;
  ImageFormat depthFormat = ImageFormat::D32Float;
  u8 sampleCount = 1;
  u32 layerCount = 1;

  RenderTarget() = default;
  RenderTarget(ImageFormat color, ImageFormat depth, u8 samples)
      : colorFormat(color), depthFormat(depth), sampleCount(samples) {}
  RenderTarget(const RenderTargetDesc &desc)
      : role(desc.role), hasColorAttachment(desc.colorFormat.has_value()),
        hasDepthAttachment(desc.depthFormat.has_value()),
        colorFormat(desc.colorFormat.value_or(ImageFormat::BGRA8)),
        colorFormats(desc.getColorFormats()),
        depthFormat(desc.depthFormat.value_or(ImageFormat::D32Float)),
        sampleCount(desc.sampleCount), layerCount(desc.layerCount) {}

  RenderTargetDesc toDesc() const {
    RenderTargetDesc desc;
    desc.role = role;
    desc.colorFormat =
        hasColorAttachment ? std::optional<ImageFormat>{colorFormat}
                           : std::nullopt;
    desc.colorFormats =
        hasColorAttachment ? colorFormats : std::vector<ImageFormat>{};
    if (desc.colorFormats.empty() && hasColorAttachment) {
      desc.colorFormats = {colorFormat};
    }
    desc.depthFormat =
        hasDepthAttachment ? std::optional<ImageFormat>{depthFormat}
                           : std::nullopt;
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
offscreen/depth-only 的 role、attachment presence 和 layerCount 语义。
*/

} // namespace LX_core
