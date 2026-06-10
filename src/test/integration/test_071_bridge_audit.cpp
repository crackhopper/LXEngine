#include "core/frame_graph/render_queue.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <iostream>
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

void testDefaultRasterQueueHasIndirectBridge() {
  AuditResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  AuditResource index(ResourceType::IndexBuffer, StringID{}, 48);
  AuditResource camera(ResourceType::UniformBuffer, StringID("CameraUBO"), 64);
  AuditResource material(ResourceType::UniformBuffer,
                         StringID("MaterialParams"), 64);

  RenderWorkQueue queue;
  queue.addItem(makeDefaultPathDraw(vertex, index, camera, material, 0));
  queue.addItem(makeDefaultPathDraw(vertex, index, camera, material, 3));

  const std::vector<RenderIndirectBatch> batches =
      queue.compileIndirectBatches();
  EXPECT(!batches.empty(),
         "default raster draw queue should expose an indirect bridge");
  EXPECT(batches.size() == 1,
         "same pipeline and descriptor resources should stay in one bridge "
         "batch");
  EXPECT(batches.front().commands.size() == queue.getItems().size(),
         "bridge batch should cover every default draw item");
  EXPECT(batches.front().commands[0].indexCount == 3,
         "bridge should preserve indexed draw command fields");
  EXPECT(batches.front().commands[1].firstIndex == 3,
         "bridge should preserve per-draw firstIndex");
}

} // namespace

int main() {
  testDefaultRasterQueueHasIndirectBridge();
  if (g_failures != 0) {
    std::cerr << g_failures << " REQ-071 bridge audit checks failed\n";
    return 1;
  }
  return 0;
}
