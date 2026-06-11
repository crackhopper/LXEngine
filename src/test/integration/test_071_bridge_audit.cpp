#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <iostream>
#include <memory>
#include <vector>

using namespace LX_core;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

struct AuditResource final : public IGpuResource {
  AuditResource(ResourceType type, StringID bindingName, u32 byteSize)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type = ResourceType::None;
  StringID bindingName;
  std::vector<u8> bytes;
};

RenderWorkItem makeDefaultPathDraw(const IGpuResource &vertex,
                                   const IGpuResource &index,
                                   const IGpuResource &camera,
                                   const IGpuResource &material,
                                   u32 firstIndex) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  item.pass = StringID("Forward");
  RenderTargetDesc target;
  target.role = RenderTargetRole::Swapchain;
  target.colorFormat = ImageFormat::BGRA8;
  target.depthFormat = ImageFormat::D32Float;
  item.target = target;
  item.debugId = StringID("071.audit.draw");
  item.objectSignature = StringID("071.audit.mesh");
  item.materialSignature = StringID("071.audit.material");
  item.pipelineKey =
      PipelineKey::build(item.objectSignature, item.materialSignature,
                         item.target.getPipelineSignature());
  item.raster.vertexBuffer = GpuResourceRef{vertex};
  item.raster.indexBuffer = GpuResourceRef{index};
  item.raster.indexCount = 3;
  item.raster.firstIndex = firstIndex;
  item.raster.instanceCount = 1;
  item.descriptorResources.emplace_back(camera);
  item.descriptorResources.emplace_back(material);
  return item;
}

void testMigratedQueueRejectsDrawDataFallback() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  RenderWorkItem item = makeDefaultPathDraw(vertex, index, camera, material, 0);
  item.raster.drawData = std::make_shared<PerDrawData>();
  queue.addItem(std::move(item));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok,
         "migrated validation queue must reject per-draw drawData fallback");
  EXPECT(!result.diagnostics.empty(),
         "migrated validation queue must explain rejected fallback");

  const BindlessSubmissionDecision decision = decideBindlessSubmission(
      queue, StringID("Forward"), true, true);
  EXPECT(decision.kind ==
             BindlessSubmissionDecisionKind::StrictValidationRejected,
         "renderer decision should reject migrated drawData fallback in strict "
         "validation mode");
}

void testMigratedQueueAcceptsFullyCoveredIndirectBatch() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  queue.addItem(makeDefaultPathDraw(vertex, index, camera, material, 0));
  queue.addItem(makeDefaultPathDraw(vertex, index, camera, material, 3));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(result.ok, "fully covered migrated queue should pass bindless audit");
  EXPECT(result.coveredItemCount == queue.getItems().size(),
         "audit should cover every migrated draw item");

  const BindlessSubmissionDecision decision = decideBindlessSubmission(
      queue, StringID("Forward"), true, true);
  EXPECT(decision.kind == BindlessSubmissionDecisionKind::BindlessBatch,
         "renderer decision should submit fully covered migrated queue as a "
         "bindless batch");
}

} // namespace

int main() {
  testMigratedQueueRejectsDrawDataFallback();
  testMigratedQueueAcceptsFullyCoveredIndirectBatch();
  if (g_failures != 0) {
    std::cerr << g_failures << " REQ-071 bridge audit checks failed\n";
    return 1;
  }
  return 0;
}
