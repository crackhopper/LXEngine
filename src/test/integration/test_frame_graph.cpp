#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/graph_resource_registry.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/frame_graph/render_work_compiler.hpp"

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

FramePass makePass(StringID name, u32 order) {
  FramePass pass;
  pass.name = name;
  pass.stableOrder = order;
  pass.target = RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  pass.stage = RenderPassStage::Raster;
  pass.dispatch = RenderPassDispatch::Fullscreen;
  pass.input.kind = RenderPassInputKind::FullscreenTriangle;
  return pass;
}

void testFrameGraphOrdersProducerBeforeConsumer() {
  GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  registry.registerResource("scene.color");

  FramePass producer = makePass(StringID("Producer"), 1);
  producer.writes.push_back(FrameGraphWrite{
      FrameGraphResourceRef::colorAttachment(StringID("scene.color")),
      std::nullopt});
  FramePass consumer = makePass(StringID("Consumer"), 0);
  consumer.reads.push_back(
      FrameGraphRead::sampled(StringID("scene.color"), StringID("SceneColor")));

  FrameGraph graph;
  graph.addPass(std::move(consumer));
  graph.addPass(std::move(producer));

  const CompiledFrameGraph compiled = graph.compile(registry);
  EXPECT(compiled.isValid(), "graph with producer and consumer should compile");
  EXPECT(compiled.getPasses().size() == 2,
         "compiled graph should contain both passes");
  EXPECT(compiled.getPasses()[0].name == StringID("Producer"),
         "producer should execute before consumer");
  EXPECT(compiled.getPasses()[1].name == StringID("Consumer"),
         "consumer should execute after producer");
}

void testFrameGraphRejectsUnknownResourceRead() {
  GraphResourceRegistry registry = GraphResourceRegistry::makeDefault();
  FramePass pass = makePass(StringID("Consumer"), 0);
  pass.reads.push_back(
      FrameGraphRead::sampled(StringID("missing.target"), StringID("Missing")));

  FrameGraph graph;
  graph.addPass(std::move(pass));

  const CompiledFrameGraph compiled = graph.compile(registry);
  EXPECT(!compiled.isValid(), "unknown source should fail graph compile");
  EXPECT(!compiled.getErrors().empty(),
         "unknown source should produce diagnostic text");
}

void testFullscreenPassProducesAcceptedDesc() {
  FramePass pass = makePass(Pass_PostProcess, 0);
  pass.shaderUri = ResourceUri("memory://postprocess.fullscreen");
  RenderWorkCompiler compiler;
  std::vector<std::unique_ptr<RenderInput>> inputs;
  const RenderWorkBuildContext context =
      RenderWorkBuildContext::realtimeEmpty();

  compiler.buildInputs(pass, context, inputs);
  const std::vector<RenderInputDesc> descs =
      compiler.prepare(pass, context, inputs);

  EXPECT(inputs.size() == 1, "fullscreen pass should produce one input");
  EXPECT(descs.size() == 1, "fullscreen pass should produce one desc");
  EXPECT(!descs.front().accepted(),
         "fullscreen desc without shader facts should fail fast");
  EXPECT(descs.front().stats.compilerInputCount == 1,
         "desc stats should count compiler inputs");
  EXPECT(descs.front().stats.rejectedInputCount == 1,
         "desc stats should count rejected inputs");
}

void testRuntimeTargetSyncUpdatesAttachmentContractsAndSignature() {
  FramePass pass = makePass(Pass_PostProcess, 0);
  pass.target =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  pass.attachments.push_back(RenderPathAttachmentContract{
      .target = "swapchain.color",
      .format = ImageFormat::BGRA8,
      .samples = 1,
      .layers = 1,
      .depth = false,
  });
  pass.renderPathNodeSignature = getFramePassRenderPathNodeSignature(pass);
  const StringID unormSignature = pass.renderPathNodeSignature;

  pass.target = RenderTargetDesc::swapchain(ImageFormat::BGRA8Srgb,
                                            ImageFormat::D32Float);
  syncFramePassAttachmentContractsWithTarget(pass);

  EXPECT(pass.attachments.size() == 1, "sync should preserve attachment count");
  EXPECT(pass.attachments.front().format == ImageFormat::BGRA8Srgb,
         "swapchain attachment contract should follow runtime sRGB target");
  const StringID srgbSignature = getFramePassRenderPathNodeSignature(pass);
  EXPECT(srgbSignature != unormSignature,
         "runtime target sync should invalidate the cached pipeline signature");
}

} // namespace

int main() {
  testFrameGraphOrdersProducerBeforeConsumer();
  testFrameGraphRejectsUnknownResourceRead();
  testFullscreenPassProducesAcceptedDesc();
  testRuntimeTargetSyncUpdatesAttachmentContractsAndSignature();
  if (g_failures != 0) {
    std::cerr << g_failures << " frame graph checks failed\n";
    return 1;
  }
  return 0;
}
