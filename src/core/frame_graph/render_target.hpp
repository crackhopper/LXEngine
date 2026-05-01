#pragma once

#include "core/rhi/image_format.hpp"
#include "core/platform/types.hpp"
#include <cstddef>
#include <cstdint>

namespace LX_core {

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

  bool operator==(const RenderTarget &other) const {
    return colorFormat == other.colorFormat &&
           depthFormat == other.depthFormat &&
           sampleCount == other.sampleCount;
  }
  bool operator!=(const RenderTarget &other) const { return !(*this == other); }
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
