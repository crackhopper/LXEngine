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
    return StringID("RenderTarget:" + std::to_string(getHash()));
  }
};

/*
@source_analysis.section RenderTarget：未完成的蓝图占位
当前 `RenderTarget` 是早期占位实现，不是设计成熟的类型。它持有三个字段
（colorFormat、depthFormat、sampleCount），既没区分 *descriptor*（结构性形状）
和 *binding*（实际 attachment 句柄），也不持有任何 GPU 资源 — 因为下游真正
消费它的代码路径还没写完。

之所以现在文档单独把它列出来，是因为虽然类型很薄，但 `Camera::matchesTarget`、
`Scene::getSceneLevelResources`、`RenderQueue::buildFromScene` 都已经在依赖它做
REQ-009 的"target 轴"筛选。也就是说：契约入口已经摆好，但契约本身还没发育完整。

详细的设计走向、字段缺口、与 PipelineKey 的接入方式由 REQ-042 收口，
正在用文档先于代码的方式拍板。本类型在 REQ-042 落地后会拆为
`RenderTargetDesc`（intern-friendly 形状，参与 PipelineKey 三级 compose）和
`RenderTarget`（持有 desc + IGpuResource 句柄 + extent）两个类型。
*/
struct RenderTarget {
  ImageFormat colorFormat = ImageFormat::BGRA8;
  ImageFormat depthFormat = ImageFormat::D32Float;
  u8 sampleCount = 1;

  RenderTarget() = default;
  RenderTarget(ImageFormat color, ImageFormat depth, u8 samples)
      : colorFormat(color), depthFormat(depth), sampleCount(samples) {}
  RenderTarget(const RenderTargetDesc &desc)
      : colorFormat(desc.colorFormat.value_or(ImageFormat::BGRA8)),
        depthFormat(desc.depthFormat.value_or(ImageFormat::D32Float)),
        sampleCount(desc.sampleCount) {}

  RenderTargetDesc toDesc() const {
    RenderTargetDesc desc;
    desc.colorFormat = colorFormat;
    desc.depthFormat = depthFormat;
    desc.sampleCount = sampleCount;
    return desc;
  }

  operator RenderTargetDesc() const { return toDesc(); }

  bool operator==(const RenderTarget &other) const {
    return colorFormat == other.colorFormat &&
           depthFormat == other.depthFormat &&
           sampleCount == other.sampleCount;
  }
  bool operator!=(const RenderTarget &other) const { return !(*this == other); }

  usize getHash() const { return toDesc().getHash(); }
};

/*
@source_analysis.section operator==：当前 REQ-009 target 轴的事实层
`RenderTarget::operator==` 是 field-by-field 比较，被 `Camera::matchesTarget`
作为 REQ-009 两轴筛选 *target 轴* 的判定。

但要老实说：现状下整条 target 轴几乎是占位 hook —— 全工程实际只用到一种默认
构造的 RenderTarget，所有 pass 和 seed Camera 默认值相同，`matchesTarget`
永远返回 true，没有真实筛选发生。这不是设计成果，是因为 RenderTarget 还没长出
足够字段（MRT、layer、自定义 extent、load/store ops 都缺）来产生真实差异。

REQ-042 落地后，这个 `==` 会被 `RenderTargetDesc::operator==` 取代，进入真实
工作状态。届时字段扩展时同步更新 `==` 与 `getPipelineSignature` 是必须项。
*/

} // namespace LX_core
