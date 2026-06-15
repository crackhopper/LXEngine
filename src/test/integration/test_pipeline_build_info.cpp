#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/image_format.hpp"
#include "core/utils/env.hpp"

#include <iostream>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

PipelineKey testKey(const char *name) { return PipelineKey{StringID(name)}; }

std::vector<ShaderStageCode> graphicsStages() {
  return {
      ShaderStageCode{ShaderStage::Vertex, std::vector<u32>{0x07230203, 1}},
      ShaderStageCode{ShaderStage::Fragment, std::vector<u32>{0x07230203, 2}},
  };
}

std::vector<ShaderStageCode> computeStages() {
  return {
      ShaderStageCode{ShaderStage::Compute, std::vector<u32>{0x07230203, 3}},
  };
}

std::vector<ShaderResourceBinding> testBindings() {
  return {
      ShaderResourceBinding{"Camera",
                            0,
                            0,
                            ShaderPropertyType::UniformBuffer,
                            1,
                            192,
                            0,
                            ShaderStage::Vertex,
                            {}},
      ShaderResourceBinding{"SceneMaterials",
                            1,
                            4,
                            ShaderPropertyType::StorageBuffer,
                            1,
                            96,
                            0,
                            ShaderStage::Fragment,
                            {}},
  };
}

VertexLayout testVertexLayout() {
  return VertexLayout(
      {
          VertexLayoutItem{"inPos", 0, DataType::Float3, 12, 0},
          VertexLayoutItem{"inNormal", 1, DataType::Float3, 12, 12},
      },
      24);
}

RenderState testRenderState() {
  RenderState state;
  state.cullMode = CullMode::Front;
  state.depthTestEnable = false;
  state.blendEnable = true;
  state.srcBlend = BlendFactor::SrcAlpha;
  state.dstBlend = BlendFactor::OneMinusSrcAlpha;
  return state;
}

std::vector<RenderPathAttachmentContract> testAttachments() {
  return {
      RenderPathAttachmentContract{
          .target = "scene.hdrColor",
          .format = ImageFormat::RGBA16Float,
          .samples = 1,
          .layers = 1,
          .depth = false,
      },
      RenderPathAttachmentContract{
          .target = "scene.depth",
          .format = ImageFormat::D32Float,
          .samples = 1,
          .layers = 1,
          .depth = true,
      },
  };
}

void expectStagesEqual(const std::vector<ShaderStageCode> &actual,
                       const std::vector<ShaderStageCode> &expected) {
  EXPECT(actual.size() == expected.size(), "stage count should match");
  if (actual.size() != expected.size()) {
    return;
  }
  for (usize i = 0; i < actual.size(); ++i) {
    EXPECT(actual[i].stage == expected[i].stage, "stage kind should match");
    EXPECT(actual[i].bytecode == expected[i].bytecode,
           "stage bytecode should match");
  }
}

void testGraphicsPreservesDirectConstructionFacts() {
  const PipelineKey key = testKey("pipeline.graphics.direct");
  const StringID variant("variant.graphics.direct");
  const RenderTargetDesc target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA16Float);
  auto stages = graphicsStages();
  auto bindings = testBindings();
  const VertexLayout vertexLayout = testVertexLayout();
  const RenderState renderState = testRenderState();
  const auto attachments = testAttachments();

  const PipelineBuildDesc desc = PipelineBuildDesc::graphics(
      key, variant, target, stages, bindings, vertexLayout, renderState,
      PrimitiveTopology::LineList, RenderPathNodeRenderingMode::Dynamic,
      attachments);

  EXPECT(desc.type == PipelineBuildType::Graphics, "type should be graphics");
  EXPECT(desc.key == key, "key should be preserved");
  EXPECT(desc.shaderVariantKey == variant,
         "shader variant should be preserved");
  EXPECT(desc.target == target, "target should be preserved");
  expectStagesEqual(desc.stages, stages);
  EXPECT(desc.bindings == bindings, "bindings should be preserved");
  EXPECT(desc.vertexLayout == vertexLayout,
         "vertex layout should be preserved");
  EXPECT(desc.renderState == renderState, "render state should be preserved");
  EXPECT(desc.topology == PrimitiveTopology::LineList,
         "topology should be preserved");
  EXPECT(desc.renderingMode == RenderPathNodeRenderingMode::Dynamic,
         "rendering mode should be preserved");
  EXPECT(desc.attachments.size() == attachments.size(),
         "attachments should be preserved");
  EXPECT(desc.pushConstant.size == 128,
         "graphics desc should keep default graphics push constant");
  EXPECT(desc.pushConstant.stageFlagsMask ==
             (static_cast<ShaderStageMask32>(ShaderStage::Vertex) |
              static_cast<ShaderStageMask32>(ShaderStage::Fragment)),
         "graphics push constant should use vertex|fragment stage mask");
}

void testComputePreservesDirectConstructionFacts() {
  const PipelineKey key = testKey("pipeline.compute.direct");
  const StringID variant("variant.compute.direct");
  auto stages = computeStages();
  auto bindings = testBindings();

  const PipelineBuildDesc desc =
      PipelineBuildDesc::compute(key, variant, stages, bindings);

  EXPECT(desc.type == PipelineBuildType::Compute, "type should be compute");
  EXPECT(desc.key == key, "key should be preserved");
  EXPECT(desc.shaderVariantKey == variant,
         "shader variant should be preserved");
  expectStagesEqual(desc.stages, stages);
  EXPECT(desc.bindings == bindings, "bindings should be preserved");
  EXPECT(desc.pushConstant.size == 0,
         "compute desc should use zero-sized push constant");
  EXPECT(desc.pushConstant.stageFlagsMask ==
             static_cast<ShaderStageMask32>(ShaderStage::Compute),
         "compute desc should use compute stage mask");
}

void testDirectConstructionIsDeterministic() {
  const PipelineKey key = testKey("pipeline.graphics.deterministic");
  const StringID variant("variant.graphics.deterministic");
  const RenderTargetDesc target =
      RenderTargetDesc::offscreenColor(ImageFormat::RGBA8);
  const auto stages = graphicsStages();
  const auto bindings = testBindings();
  const VertexLayout vertexLayout = testVertexLayout();
  const RenderState renderState = testRenderState();
  const auto attachments = testAttachments();

  const PipelineBuildDesc a = PipelineBuildDesc::graphics(
      key, variant, target, stages, bindings, vertexLayout, renderState,
      PrimitiveTopology::TriangleList, RenderPathNodeRenderingMode::Traditional,
      attachments);
  const PipelineBuildDesc b = PipelineBuildDesc::graphics(
      key, variant, target, stages, bindings, vertexLayout, renderState,
      PrimitiveTopology::TriangleList, RenderPathNodeRenderingMode::Traditional,
      attachments);

  EXPECT(a.type == b.type, "type should be deterministic");
  EXPECT(a.key == b.key, "key should be deterministic");
  EXPECT(a.shaderVariantKey == b.shaderVariantKey,
         "shader variant should be deterministic");
  EXPECT(a.target == b.target, "target should be deterministic");
  expectStagesEqual(a.stages, b.stages);
  EXPECT(a.bindings == b.bindings, "bindings should be deterministic");
  EXPECT(a.vertexLayout == b.vertexLayout,
         "vertex layout should be deterministic");
  EXPECT(a.renderState == b.renderState,
         "render state should be deterministic");
  EXPECT(a.topology == b.topology, "topology should be deterministic");
  EXPECT(a.renderingMode == b.renderingMode,
         "rendering mode should be deterministic");
  EXPECT(a.attachments.size() == b.attachments.size(),
         "attachments should be deterministic");
}

void testSrgbSwapchainTargetKeepsDistinctPipelineSignature() {
  const RenderTargetDesc unorm =
      RenderTargetDesc::swapchain(ImageFormat::BGRA8, ImageFormat::D32Float);
  const RenderTargetDesc srgb = RenderTargetDesc::swapchain(
      ImageFormat::BGRA8Srgb, ImageFormat::D32Float);

  EXPECT(unorm != srgb, "sRGB swapchain format must not collapse to UNORM");
  EXPECT(unorm.getPipelineSignature() != srgb.getPipelineSignature(),
         "sRGB swapchain target needs a distinct pipeline identity");
}

} // namespace

int main() {
  expSetEnvVK();

  testGraphicsPreservesDirectConstructionFacts();
  testComputePreservesDirectConstructionFacts();
  testDirectConstructionIsDeterministic();
  testSrgbSwapchainTargetKeepsDistinctPipelineSignature();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all pipeline_build_info tests passed\n";
  return 0;
}
