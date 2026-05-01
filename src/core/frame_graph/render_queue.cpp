#include "core/frame_graph/render_queue.hpp"

#include "core/asset/mesh.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <unordered_set>

namespace LX_core {

namespace {

/*
@source_analysis.section makeItemFromValidatedData：把 validated 数据原样翻译成 RenderingItem
这是一个 anonymous-namespace 内的纯字段拷贝函数，存在的理由是把"翻译"这件事
和"过滤 + 入队"分开：`buildFromScene` 只关心条件判断和 sceneResources 追加，
逐字段拷贝从这里走。

不做合并、不做校验，因为 `ValidatedRenderablePassData` 的命名已经承诺了
"pass-level validation 已经完成"。这里把它视作只读事实，原样转成 backend 消费的
`RenderingItem`，连字段顺序都保持一一对应，方便审阅。
*/
RenderingItem makeItemFromValidatedData(const ValidatedRenderablePassData &data) {
  RenderingItem item;
  item.vertexBuffer = data.vertexBuffer;
  item.indexBuffer = data.indexBuffer;
  item.drawData = data.drawData;
  item.descriptorResources = data.descriptorResources;
  item.shaderInfo = data.shaderInfo;
  item.material = data.material;
  item.pass = data.pass;
  item.pipelineKey = data.pipelineKey;
  return item;
}

} // namespace

void RenderQueue::addItem(RenderingItem item) {
  m_items.push_back(std::move(item));
}

void RenderQueue::clearItems() { m_items.clear(); }

void RenderQueue::sort() {
  std::stable_sort(m_items.begin(), m_items.end(),
                   [](const RenderingItem &a, const RenderingItem &b) {
                     return a.pipelineKey.id.id < b.pipelineKey.id.id;
                   });
}

std::vector<PipelineBuildDesc>
RenderQueue::collectUniquePipelineBuildDescs() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildDesc> out;
  out.reserve(m_items.size());
  for (const auto &item : m_items) {
    if (!seen.insert(item.pipelineKey).second)
      continue;
    out.push_back(PipelineBuildDesc::fromRenderingItem(item));
  }
  return out;
}

void RenderQueue::buildFromScene(const Scene &scene, StringID pass,
                                 const RenderTarget &target) {
  clearItems();

  // REQ-009: target-filtered scene-level resources.
  auto sceneResources = scene.getSceneLevelResources(pass, target);
  const VisibilityLayerMask visibleMask =
      scene.getCombinedCameraCullingMask(target);

  for (const auto &renderable : scene.getRenderables()) {
    if (!renderable)
      continue;
    if (!renderable->supportsPass(pass))
      continue;
    if ((renderable->getVisibilityLayerMask() & visibleMask) == 0)
      continue;
    auto validated = renderable->getValidatedPassData(pass);
    if (!validated)
      continue;

    RenderingItem item = makeItemFromValidatedData(validated->get());

    item.descriptorResources.insert(item.descriptorResources.end(),
                                    sceneResources.begin(),
                                    sceneResources.end());

    m_items.push_back(std::move(item));
  }

  sort();
}

} // namespace LX_core
