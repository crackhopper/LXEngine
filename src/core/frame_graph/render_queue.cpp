#include "core/frame_graph/render_queue.hpp"

#include "core/asset/mesh.hpp"
#include "core/asset/render_effect.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/scene_descriptor_resource_resolver.hpp"
#include "core/offline/offline_scene_storage_resources.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/scene.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace LX_core {

namespace {

/*
@source_analysis.section makeItemFromValidatedData：把 validated 结构数据翻译成
RenderWorkItem 这是一个 anonymous-namespace
内的纯字段拷贝函数，存在的理由是把"翻译"这件事 和"过滤 + 入队"分开：`build`
只关心条件判断和 sceneResources 追加， 逐字段拷贝从这里走。

不做合并、不做校验，因为 `ValidatedRenderablePassData` 的命名已经承诺了
"pass-level validation 已经完成"。这里把 pass 内可验证的结构事实转成 backend
消费的 `RenderWorkItem`；target-dependent 的 `pipelineKey` 不在 validated
数据里缓存，而是在 `build` 拿到 target 后再组合。
*/
RenderWorkItem
makeItemFromValidatedData(const ValidatedRenderablePassData &data) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  item.raster.vertexBuffer = data.vertexBuffer;
  item.raster.indexBuffer = data.indexBuffer;
  item.shaderProgram = data.shaderProgram;
  item.shaderInfo = data.shaderInfo;
  item.renderState = data.renderState;
  item.sortCenter = data.sortCenter;
  item.pass = data.pass;
  item.objectSignature = data.objectSignature;
  item.materialSignature = data.materialSignature;
  item.materialTypeVariant = data.materialTypeVariant;
  item.renderPathNodeSignature = data.renderPathNodeSignature;
  return item;
}

[[nodiscard]] StringID makeNamedMaterialTypeVariant(std::string_view name,
                                                    StringID shaderSignature) {
  auto &tbl = GlobalStringTable::get();
  StringID fields[] = {
      tbl.Intern(name),
      tbl.Intern("<non-bsdf-source-uri>"),
      tbl.Intern("<non-bsdf-reflection-hash>"),
      tbl.Intern("<non-bsdf-source-signature>"),
      shaderSignature,
  };
  return tbl.compose(TypeTag::MaterialTypeVariant, fields);
}

[[nodiscard]] bool sameResourceRef(const DescriptorResourceRef &a,
                                   const DescriptorResourceRef &b) {
  if (a.kind() != b.kind()) {
    return false;
  }
  if (a.getBindingName() != b.getBindingName()) {
    return false;
  }
  if (a.isResource() || b.isResource()) {
    return a.isResource() && b.isResource() && a.resource().isValid() &&
           b.resource().isValid() &&
           a.resource().getBackendCacheIdentity() ==
               b.resource().getBackendCacheIdentity();
  }
  if (a.isTextureArray()) {
    return a.textures().size() == b.textures().size();
  }
  return true;
}

[[nodiscard]] bool sameDescriptorResources(const DescriptorResourceList &a,
                                           const DescriptorResourceList &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (usize i = 0; i < a.size(); ++i) {
    if (!sameResourceRef(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] u32 resolveIndexCount(const RasterDrawWorkPayload &raster) {
  if (raster.indexCount != 0) {
    return raster.indexCount;
  }
  if (!raster.indexBuffer.isValid()) {
    return 0;
  }
  return static_cast<u32>(raster.indexBuffer.get().getByteSize() / sizeof(u32));
}

[[nodiscard]] IndexedIndirectDrawCommand
makeIndirectCommand(const RasterDrawWorkPayload &raster) {
  IndexedIndirectDrawCommand command;
  command.indexCount = resolveIndexCount(raster);
  command.instanceCount = raster.instanceCount;
  command.firstIndex = raster.firstIndex;
  command.vertexOffset = raster.vertexOffset;
  command.firstInstance =
      raster.drawRecordIndex == u32_max ? 0u : raster.drawRecordIndex;
  return command;
}

[[nodiscard]] std::optional<u32>
tryResolveGpuMaterialIndex(const SceneResourceTableUploadView &uploadView,
                           MaterialHandle handle) {
  const auto it =
      std::find_if(uploadView.materialIndexByHandle.begin(),
                   uploadView.materialIndexByHandle.end(),
                   [handle](const SceneResourceMaterialUploadIndex &entry) {
                     return entry.handle == handle;
                   });
  if (it == uploadView.materialIndexByHandle.end() ||
      it->typedIndex >= uploadView.materials.size()) {
    return std::nullopt;
  }
  return it->typedIndex;
}

[[nodiscard]] bool
shaderConsumesBinding(const IShaderSharedPtr &shader, std::string_view name) {
  if (!shader) {
    return false;
  }
  for (const auto &binding : shader->getReflectionBindings()) {
    if (binding.name == name) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<u32>
tryResolveGpuObjectIndex(const SceneResourceTableUploadView &uploadView,
                         ObjectHandle handle) {
  const auto it =
      std::find_if(uploadView.objectIndexByHandle.begin(),
                   uploadView.objectIndexByHandle.end(),
                   [handle](const SceneResourceObjectUploadIndex &entry) {
                     return entry.handle == handle;
                   });
  if (it != uploadView.objectIndexByHandle.end() &&
      it->typedIndex < uploadView.objects.size() &&
      it->typedIndex < uploadView.draws.size()) {
    return it->typedIndex;
  }
  return std::nullopt;
}

[[nodiscard]] bool layoutHasAttribute(const VertexLayout &layout, u32 location,
                                      DataType type) {
  for (const VertexLayoutItem &item : layout.getItems()) {
    if (item.location == location && item.type == type) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool layoutMatchesVertexContract(
    const VertexLayout &layout, RenderPathGeometryVertexContract contract) {
  if (!layoutHasAttribute(layout, 0, DataType::Float3)) {
    return false;
  }
  if (contract == RenderPathGeometryVertexContract::PositionOnly) {
    return true;
  }
  return layoutHasAttribute(layout, 1, DataType::Float3) &&
         layoutHasAttribute(layout, 2, DataType::Float2) &&
         layoutHasAttribute(layout, 3, DataType::Float4);
}

void validateGeometryContract(const RenderWorkItem &item,
                              const RenderPathGeometryContract &contract) {
  if (item.kind != RenderWorkKind::RasterDraw) {
    return;
  }
  if (!item.raster.vertexBuffer.isValid()) {
    throw std::logic_error(
        "RenderWorkQueue geometry contract requires a vertex buffer");
  }
  if (!item.raster.indexBuffer.isValid()) {
    throw std::logic_error(
        "RenderWorkQueue geometry contract requires an index buffer");
  }

  const auto *vertexBuffer =
      dynamic_cast<const IVertexBuffer *>(&item.raster.vertexBuffer.get());
  if (vertexBuffer == nullptr) {
    throw std::logic_error(
        "RenderWorkQueue geometry contract received non-vertex resource");
  }
  if (!layoutMatchesVertexContract(vertexBuffer->getLayout(),
                                   contract.vertex)) {
    throw std::logic_error(
        "RenderWorkQueue geometry vertex contract mismatch for item " +
        GlobalStringTable::get().toDebugString(item.debugId));
  }

  const auto *indexBuffer =
      dynamic_cast<const IndexBuffer *>(&item.raster.indexBuffer.get());
  if (indexBuffer == nullptr) {
    throw std::logic_error(
        "RenderWorkQueue geometry contract received non-index resource");
  }
  if (indexBuffer->getTopology() != contract.topology) {
    throw std::logic_error(
        "RenderWorkQueue geometry topology contract mismatch for item " +
        GlobalStringTable::get().toDebugString(item.debugId));
  }
}

[[nodiscard]] bool canAppendToBatch(const RenderIndirectBatch &batch,
                                    const RenderWorkItem &item) {
  return batch.pipelineKey == item.pipelineKey && batch.pass == item.pass &&
         batch.target == item.target &&
         batch.vertexBuffer.isValid() == item.raster.vertexBuffer.isValid() &&
         batch.indexBuffer.isValid() == item.raster.indexBuffer.isValid() &&
         (!batch.vertexBuffer.isValid() ||
          &batch.vertexBuffer.get() == &item.raster.vertexBuffer.get()) &&
         (!batch.indexBuffer.isValid() ||
          &batch.indexBuffer.get() == &item.raster.indexBuffer.get()) &&
         sameDescriptorResources(batch.descriptorResources,
                                 item.descriptorResources);
}

[[nodiscard]] std::optional<Vec3f>
resolveSortCameraEye(const Scene &scene,
                     const RenderWorkBuildContext::RealtimeOptions &options,
                     const RenderTarget &target) {
  if (options.cameraResource.has_value()) {
    return options.cameraResource->pose.eye;
  }

  const RenderTarget &sceneResourceTarget =
      options.sceneResourceTarget.value_or(target);
  for (const auto &cameraNode : scene.getCameras()) {
    if (!cameraNode) {
      continue;
    }
    const auto camera = cameraNode->getComponent<CameraComponent>();
    if (!camera.has_value() || !camera->get().isActive() ||
        !camera->get().matchesTarget(sceneResourceTarget)) {
      continue;
    }
    return camera->get().getEyePosition();
  }
  return std::nullopt;
}

} // namespace

void RenderWorkQueue::addItem(RenderWorkItem item) {
  m_items.push_back(std::move(item));
}

void RenderWorkQueue::clearItems() { m_items.clear(); }

void RenderWorkQueue::sort() { sort(std::nullopt); }

void RenderWorkQueue::sort(const std::optional<Vec3f> &cameraEye) {
  std::stable_sort(
      m_items.begin(), m_items.end(),
      [cameraEye](const RenderWorkItem &a, const RenderWorkItem &b) {
        const bool aTransparent = a.renderState.blendEnable;
        const bool bTransparent = b.renderState.blendEnable;
        if (aTransparent != bTransparent) {
          return !aTransparent;
        }
        if (cameraEye.has_value() && aTransparent && bTransparent) {
          const float aDistance = (a.sortCenter - *cameraEye).length2();
          const float bDistance = (b.sortCenter - *cameraEye).length2();
          if (aDistance != bDistance) {
            return aDistance > bDistance;
          }
        }
        return a.pipelineKey.id.id < b.pipelineKey.id.id;
      });
}

RenderWorkItem makeOfflineComputeItem(offline::OfflineRenderJob &job,
                                      StringID pass, const RenderTarget &target,
                                      IShaderSharedPtr shader,
                                      StringID renderPathNodeSignature) {
  offline::OfflineSceneStorageResources storageResources =
      offline::buildOfflineSceneStorageResources(job);
  RenderWorkItem item;
  item.domain = RenderDomain::Offline;
  item.kind = RenderWorkKind::ComputeDispatch;
  item.pass = pass;
  item.target = target.toDesc();
  item.compute.groupCountX = (job.output.width + 7u) / 8u;
  item.compute.groupCountY = (job.output.height + 7u) / 8u;
  item.compute.groupCountZ = 1u;
  item.shaderInfo = std::move(shader);
  item.descriptorResources = std::move(storageResources.descriptorResources);
  item.debugId = StringID("OfflineRayTraceDispatch");
  item.objectSignature = StringID("OfflineSceneGpuData");
  item.materialSignature = StringID("OfflinePrimaryRayCompute");
  item.materialTypeVariant = makeNamedMaterialTypeVariant(
      "offline-primary-ray", item.shaderProgram.getPipelineSignature());
  item.renderPathNodeSignature = renderPathNodeSignature;
  item.pipelineKey =
      PipelineKey::build(item.materialTypeVariant, item.renderPathNodeSignature);
  return item;
}

std::vector<PipelineBuildDesc>
RenderWorkQueue::collectUniquePipelineBuildDescs() const {
  std::unordered_set<PipelineKey, PipelineKey::Hash> seen;
  std::vector<PipelineBuildDesc> out;
  out.reserve(m_items.size());
  for (const auto &item : m_items) {
    if (!seen.insert(item.pipelineKey).second)
      continue;
    out.push_back(PipelineBuildDesc::fromRenderWorkItem(item));
  }
  return out;
}

std::vector<RenderIndirectBatch>
RenderWorkQueue::compileIndirectBatches() const {
  std::vector<RenderIndirectBatch> batches;
  for (usize itemIndex = 0; itemIndex < m_items.size(); ++itemIndex) {
    const RenderWorkItem &item = m_items[itemIndex];
    if (item.kind != RenderWorkKind::RasterDraw ||
        !item.raster.vertexBuffer.isValid() ||
        !item.raster.indexBuffer.isValid()) {
      continue;
    }
    IndexedIndirectDrawCommand command = makeIndirectCommand(item.raster);
    if (command.indexCount == 0 || command.instanceCount == 0) {
      continue;
    }
    if (batches.empty() || !canAppendToBatch(batches.back(), item)) {
      RenderIndirectBatch batch;
      batch.pipelineKey = item.pipelineKey;
      batch.pass = item.pass;
      batch.target = item.target;
      batch.descriptorResources = item.descriptorResources;
      batch.vertexBuffer = item.raster.vertexBuffer;
      batch.indexBuffer = item.raster.indexBuffer;
      batches.push_back(std::move(batch));
    }
    batches.back().commands.push_back(command);
    batches.back().sourceItemIndices.push_back(itemIndex);
  }
  return batches;
}

void RenderWorkQueue::build(const RenderWorkBuildContext &context,
                            StringID pass, const RenderTarget &target,
                            StringID renderPathNodeSignature,
                            std::optional<RenderPathGeometryContract>
                                geometryContract,
                            std::optional<RenderPathNodeRenderingMode>
                                renderingMode,
                            std::vector<RenderPathAttachmentContract>
                                attachments) {
  if (context.domain() == RenderDomain::Offline) {
    clearItems();
    if (pass == Pass_OfflineRayTrace) {
      m_items.push_back(
          makeOfflineComputeItem(context.offlineJob(), pass, target,
                                 context.offlineJob().offlineShader,
                                 renderPathNodeSignature));
    }
    return;
  }

  const Scene &scene = context.realtimeScene();
  const auto &options = context.realtimeOptions();
  DescriptorResourceList sceneResources;
  VisibilityLayerMask visibleMask = 0;
  if (options.cameraResource.has_value()) {
    sceneResources =
        scene.getSceneLevelResources(pass, *options.cameraResource);
    visibleMask = options.cameraResource->cullingMask;
  } else {
    const RenderTarget &sceneResourceTarget =
        options.sceneResourceTarget.value_or(target);
    sceneResources = scene.getSceneLevelResources(pass, sceneResourceTarget);
    visibleMask = scene.getCombinedCameraCullingMask(sceneResourceTarget);
  }
  if (options.visibleMask.has_value()) {
    visibleMask = *options.visibleMask;
  }
  if (visibleMask == 0 && pass == Pass_Shadow) {
    visibleMask = VisibilityMask_All;
  }

  const std::optional<Vec3f> cameraEye =
      resolveSortCameraEye(scene, options, target);
  buildRealtime(scene, pass, target, renderPathNodeSignature, geometryContract,
                renderingMode, std::move(attachments),
                std::move(sceneResources), visibleMask, cameraEye);
}

void RenderWorkQueue::buildRealtime(const Scene &scene, StringID pass,
                                    const RenderTarget &target,
                                    StringID renderPathNodeSignature,
                                    std::optional<RenderPathGeometryContract>
                                        geometryContract,
                                    std::optional<RenderPathNodeRenderingMode>
                                        renderingMode,
                                    std::vector<RenderPathAttachmentContract>
                                        attachments,
                                    DescriptorResourceList sceneResources,
                                    VisibilityLayerMask visibleMask,
                                    std::optional<Vec3f> cameraEye) {
  clearItems();
  const SceneResourceTableUploadView uploadView =
      scene.resources().buildUploadView();

  for (const auto &renderable : scene.getRenderables()) {
    if (!renderable)
      continue;
    if (renderable->isDebugOnlyRenderable() && pass != Pass_DebugOverlay)
      continue;
    if (!renderable->supportsPass(pass))
      continue;
    if ((renderable->getVisibilityLayerMask() & visibleMask) == 0)
      continue;
    auto validated = renderable->getValidatedPassData(pass);
    if (!validated)
      continue;

    const auto &validatedData = validated->get();
    RenderWorkItem item = makeItemFromValidatedData(validatedData);
    const bool needsMaterialRecord =
        shaderConsumesBinding(item.shaderInfo, "SceneMaterials");
    if (validatedData.materialHandle.isValid()) {
      if (const auto materialIndex =
              tryResolveGpuMaterialIndex(uploadView,
                                         validatedData.materialHandle)) {
        item.raster.materialIndex = *materialIndex;
      } else if (needsMaterialRecord) {
        throw std::logic_error(
            "RenderWorkQueue cannot resolve draw material handle to "
            "SceneMaterials index");
      }
    } else if (needsMaterialRecord) {
      throw std::logic_error(
          "RenderWorkQueue cannot bind SceneMaterials without a typed "
          "material handle");
    }
    const bool needsDrawRecord =
        shaderConsumesBinding(item.shaderInfo, "SceneDraws") ||
        shaderConsumesBinding(item.shaderInfo, "SceneObjects");
    if (validatedData.objectHandle.isValid()) {
      if (const auto drawRecordIndex =
              tryResolveGpuObjectIndex(uploadView, validatedData.objectHandle)) {
        item.raster.drawRecordIndex = *drawRecordIndex;
        if (*drawRecordIndex < uploadView.draws.size()) {
          item.raster.materialRefIndex =
              uploadView.draws[*drawRecordIndex].materialRefIndex;
        }
      } else if (needsDrawRecord) {
        throw std::logic_error(
            "RenderWorkQueue cannot resolve draw object handle to SceneDraws "
            "index");
      }
    } else if (needsDrawRecord) {
      throw std::logic_error(
          "RenderWorkQueue cannot bind SceneDraws without a typed object "
          "handle");
    }
    item.target = target.toDesc();
    item.debugId = renderable->getDebugId();
    item.renderPathNodeSignature = renderPathNodeSignature;
    item.renderingMode = renderingMode;
    item.attachments = attachments;
    item.pipelineKey =
        PipelineKey::build(item.materialTypeVariant,
                           item.renderPathNodeSignature);
    if (geometryContract.has_value()) {
      validateGeometryContract(item, *geometryContract);
    }

    item.descriptorResources =
        buildSceneDescriptorResources(SceneDescriptorResourceContext{
            .scene = scene,
            .renderable = validated->get(),
            .pass = pass,
            .target = target,
            .sceneResources = sceneResources,
        });

    m_items.push_back(std::move(item));
  }

  sort(cameraEye);
}

} // namespace LX_core
