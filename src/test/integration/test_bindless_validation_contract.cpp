#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/rhi/gpu_resource.hpp"

#include <iostream>
#include <memory>
#include <optional>
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

struct TestResource final : public IGpuResource {
  TestResource(ResourceType type, StringID bindingName, u32 byteSize)
      : type(type), bindingName(bindingName), bytes(byteSize, 0) {}

  ResourceType getType() const override { return type; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  ResourceType type = ResourceType::None;
  StringID bindingName;
  std::vector<u8> bytes;
};

RenderWorkItem makeMigratedDraw(const IGpuResource &vertex,
                                const IGpuResource &index,
                                std::optional<PerDrawDataSharedPtr> drawData) {
  RenderWorkItem item;
  item.domain = RenderDomain::Realtime;
  item.kind = RenderWorkKind::RasterDraw;
  item.pass = StringID("Forward");
  item.debugId = StringID("bindless.validation.draw");
  item.objectSignature = StringID("bindless.validation.mesh");
  item.materialSignature = StringID("bindless.validation.material");
  RenderTargetDesc target;
  target.role = RenderTargetRole::Swapchain;
  target.colorFormat = ImageFormat::BGRA8;
  target.depthFormat = ImageFormat::D32Float;
  item.target = target;
  item.pipelineKey =
      PipelineKey::build(item.objectSignature, item.materialSignature,
                         target.getPipelineSignature());
  item.raster.vertexBuffer = GpuResourceRef{vertex};
  item.raster.indexBuffer = GpuResourceRef{index};
  item.raster.indexCount = 3;
  item.raster.instanceCount = 1;
  if (drawData.has_value()) {
    item.raster.drawData = *drawData;
  }
  return item;
}

void testStrictContractAcceptsFullyCoveredBatch() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkQueue queue;
  queue.addItem(makeMigratedDraw(vertex, index, std::nullopt));
  queue.addItem(makeMigratedDraw(vertex, index, std::nullopt));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(result.ok, "strict bindless contract should accept full batch coverage");
  EXPECT(result.coveredItemCount == 2,
         "strict bindless contract should report covered items");
  EXPECT(result.diagnostics.empty(),
         "strict bindless contract should not emit diagnostics on success");
}

void testStrictContractRejectsDrawDataFallback() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  auto drawData = std::make_shared<PerDrawData>();
  RenderWorkQueue queue;
  queue.addItem(makeMigratedDraw(vertex, index, drawData));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok, "strict bindless contract should reject drawData fallback");
  EXPECT(!result.diagnostics.empty(),
         "strict bindless contract should explain the rejection");
  EXPECT(result.diagnostics.front().reason.find("drawData") != std::string::npos,
         "diagnostic should name drawData as the fallback cause");
}

void testStrictContractRejectsPartialCoverage() {
  TestResource vertex(ResourceType::VertexBuffer, StringID{}, 96);
  RenderWorkQueue queue;
  TestResource index(ResourceType::IndexBuffer, StringID{}, 48);
  RenderWorkItem item = makeMigratedDraw(vertex, index, std::nullopt);
  item.raster.indexBuffer = GpuResourceRef{};
  queue.addItem(std::move(item));

  const BindlessValidationResult result =
      validateBindlessMigratedQueue(queue, StringID("Forward"));
  EXPECT(!result.ok, "strict bindless contract should reject partial coverage");
  EXPECT(!result.diagnostics.empty(),
         "strict bindless contract should report uncovered draw item");
}

} // namespace

int main() {
  testStrictContractAcceptsFullyCoveredBatch();
  testStrictContractRejectsDrawDataFallback();
  testStrictContractRejectsPartialCoverage();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless validation contract checks failed\n";
    return 1;
  }
  return 0;
}
