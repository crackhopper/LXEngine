#include "core/scene/render_queue.hpp"

#include "core/resources/mesh.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <unordered_set>

namespace LX_core {

namespace {

/// 把一个 IRenderable + pass 展开成一个 RenderingItem。REQ-008 之前这个逻辑
/// 住在 Scene::buildRenderingItemForRenderable；现在搬到 RenderQueue 内部。
RenderingItem makeItemFromRenderable(const IRenderablePtr &renderable,
                                     StringID pass) {
  RenderingItem item;
  if (!renderable)
    return item;

  item.vertexBuffer = renderable->getVertexBuffer();
  item.indexBuffer = renderable->getIndexBuffer();
  item.objectInfo = renderable->getObjectInfo();
  item.descriptorResources = renderable->getDescriptorResources();
  item.shaderInfo = renderable->getShaderInfo();
  item.passMask = renderable->getPassMask();
  item.pass = pass;

  auto sub = std::dynamic_pointer_cast<RenderableSubMesh>(renderable);
  if (sub && sub->mesh && sub->material) {
    item.material = sub->material;
    StringID objectSig = sub->getRenderSignature(pass);
    StringID materialSig = sub->material->getRenderSignature(pass);
    item.pipelineKey = PipelineKey::build(objectSig, materialSig);
  }
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

std::vector<PipelineBuildInfo>
RenderQueue::collectUniquePipelineBuildInfos() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildInfo> out;
  out.reserve(m_items.size());
  for (const auto &item : m_items) {
    if (!seen.insert(item.pipelineKey).second)
      continue;
    out.push_back(PipelineBuildInfo::fromRenderingItem(item));
  }
  return out;
}

void RenderQueue::buildFromScene(const Scene &scene, StringID pass) {
  clearItems();

  // Scene 级资源（camera UBO + light UBO）拉取一次，复用给所有 item。
  auto sceneResources = scene.getSceneLevelResources();

  for (const auto &renderable : scene.getRenderables()) {
    if (!renderable)
      continue;
    if (!renderable->supportsPass(pass))
      continue;

    RenderingItem item = makeItemFromRenderable(renderable, pass);

    // 把 scene 级资源追加到 item.descriptorResources 末尾，保持
    // "renderable 自己的资源在前、scene 级在后" 的 binding 顺序约定。
    item.descriptorResources.insert(item.descriptorResources.end(),
                                    sceneResources.begin(),
                                    sceneResources.end());

    m_items.push_back(std::move(item));
  }

  sort();
}

} // namespace LX_core
