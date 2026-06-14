#include "core/frame_graph/render_input.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
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

struct TestResource final : public IGpuResource {
  explicit TestResource(StringID binding) : bindingName(binding) {}

  ResourceType getType() const override { return ResourceType::StorageBuffer; }
  const void *getRawData() const override { return bytes.data(); }
  u32 getByteSize() const override { return static_cast<u32>(bytes.size()); }
  StringID getBindingName() const override { return bindingName; }

  StringID bindingName;
  std::vector<u8> bytes = std::vector<u8>(64, 0);
};

PipelineBuildDesc makePipelineBuildDesc(PipelineKey key) {
  std::vector<ShaderResourceBinding> bindings{
      ShaderResourceBinding{"SceneObjects",
                            0,
                            0,
                            ShaderPropertyType::StorageBuffer,
                            1,
                            64,
                            0,
                            ShaderStage::Vertex,
                            {}}};
  return PipelineBuildDesc::graphics(
      key, StringID("validated-material-type"),
      RenderTargetDesc{.role = RenderTargetRole::Swapchain,
                       .colorFormat = ImageFormat::BGRA8,
                       .depthFormat = ImageFormat::D32Float},
      {}, std::move(bindings), VertexLayout{}, RenderState{},
      PrimitiveTopology::TriangleList, std::nullopt, {});
}

void testAcceptedDescCarriesPipelineBindingAndStats() {
  TestResource sceneObjects(StringID("SceneObjects"));
  RenderDrawInput input;
  input.pass = StringID("Forward");
  input.debugId = StringID("draw.input.0");
  input.inputIndex = 0;
  input.objectDataSignature = StringID("BindlessObjectData.v1");
  input.materialTypeSignature = StringID("matte");
  input.drawCommands.push_back(RenderDrawCommand{.indexCount = 3,
                                                 .instanceCount = 1});

  const PipelineKey key =
      PipelineKey::build(StringID("validated-material-type"),
                         StringID("validated-render-node"));
  RenderInputDesc desc;
  desc.status = RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;
  desc.pass = input.pass;
  desc.debugId = input.debugId;
  desc.pipelineKey = key;
  desc.pipelineBuildDesc = makePipelineBuildDesc(key);
  desc.bindingPlan.descriptors.emplace_back(sceneObjects);
  desc.resourceDependencies.emplace_back(sceneObjects);
  desc.stats.compilerInputCount = 1;
  desc.stats.acceptedInputCount = 1;
  desc.stats.submittedDrawCount = 1;

  std::vector<RenderInputDesc> descs{desc};
  const RenderInputValidationResult validation =
      validatePreparedRenderInputs(descs);

  EXPECT(descs.size() == 1, "desc vector should expose one prepared input");
  EXPECT(descs.front().accepted(), "desc should be accepted");
  EXPECT(descs.front().pipelineBuildDesc.key == key,
         "desc should carry pipeline build facts");
  EXPECT(!descs.front().bindingPlan.descriptors.empty(),
         "desc should carry binding plan descriptors");
  EXPECT(descs.front().stats.compilerInputCount == 1,
         "stats should count compiler inputs");
  EXPECT(descs.front().stats.acceptedInputCount == 1,
         "stats should count accepted inputs");
  EXPECT(descs.front().stats.rejectedInputCount == 0,
         "stats should count rejected inputs");
  EXPECT(descs.front().stats.submittedDrawCount == 1,
         "stats should count submitted draw commands");
  EXPECT(descs.front().stats.submittedDispatchCount == 0,
         "stats should count submitted dispatches");
  EXPECT(descs.front().stats.fallbackObservedCount == 0,
         "desc path should not observe fallback");
  EXPECT(validation.ok, "accepted desc should validate");
  EXPECT(validation.diagnostics.empty(),
         "accepted desc should not emit diagnostics");
}

} // namespace

int main() {
  testAcceptedDescCarriesPipelineBindingAndStats();
  if (g_failures != 0) {
    std::cerr << g_failures << " bindless indirect contract checks failed\n";
    return 1;
  }
  return 0;
}
