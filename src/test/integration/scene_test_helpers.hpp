#pragma once

// Shared helpers for integration tests that need desc-backed Vulkan inputs or
// small reusable scene setup.

#include "core/rhi/gpu_resource.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"
#include "core/asset/material_template.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/frame_graph.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_input.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <cassert>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace LX_test {

inline LX_core::StringID
testRenderPathNodeSignature(LX_core::StringID pass,
                            const LX_core::RenderTarget &target) {
  LX_core::FramePass framePass;
  framePass.name = pass;
  framePass.target = target.toDesc();
  return LX_core::getFramePassRenderPathNodeSignature(framePass);
}

inline LX_core::SceneNodeSharedPtr makeDefaultCameraNodeWithTarget() {
  static int cameraCounter = 0;
  auto node = LX_core::SceneNode::create(
      "test_camera_" + std::to_string(++cameraCounter));
  auto camera = node->addComponent<LX_core::CameraComponent>();
  assert(camera.has_value() && "camera component must attach");
  camera->get().setTarget(LX_core::RenderTarget{});
  camera->get().updateMatrices();
  return node;
}

inline LX_core::ShaderStageCode
loadTestShaderStage(const std::string &shaderName, const char *stageSuffix,
                    LX_core::ShaderStage stage) {
  const std::string shaderPath = getShaderPath(shaderName, stageSuffix);
  if (shaderPath.empty()) {
    throw std::runtime_error("test shader binary not found: " + shaderName +
                             "." + stageSuffix);
  }
  const auto bytes = readFile(shaderPath);
  if ((bytes.size() % sizeof(u32)) != 0) {
    throw std::runtime_error("test shader bytecode size is not 4-byte aligned: " +
                             shaderPath);
  }

  LX_core::ShaderStageCode code;
  code.stage = stage;
  code.bytecode.resize(bytes.size() / sizeof(u32));
  std::memcpy(code.bytecode.data(), bytes.data(), bytes.size());
  return code;
}

inline LX_core::IShaderSharedPtr makeMinimalShaderForVulkanTests() {
  constexpr const char *kShaderName = "minimal";
  std::vector<LX_core::ShaderStageCode> stages{
      loadTestShaderStage(kShaderName, "vert.spv", LX_core::ShaderStage::Vertex),
      loadTestShaderStage(kShaderName, "frag.spv",
                          LX_core::ShaderStage::Fragment),
  };

  return std::make_shared<LX_infra::CompiledShader>(
      stages, LX_infra::ShaderReflector::reflect(stages),
      LX_infra::ShaderReflector::reflectVertexInputs(stages), kShaderName);
}

inline LX_core::RenderDrawInput makeMinimalRenderDrawInputForVulkanTests(
    const LX_core::IVertexBuffer &vertexBuffer,
    const LX_core::IndexBuffer &indexBuffer,
    LX_core::StringID pass = LX_core::Pass_PostProcess) {
  LX_core::RenderDrawInput input;
  input.source = LX_core::RenderDrawInputSource::SceneRenderable;
  input.pass = pass;
  input.debugId = LX_core::StringID("vulkan_test_minimal_render_input");
  input.inputIndex = 0;
  input.vertexBuffer = LX_core::GpuResourceRef{vertexBuffer};
  input.indexBuffer = LX_core::GpuResourceRef{indexBuffer};
  input.drawCommands.push_back(LX_core::RenderDrawCommand{
      .indexCount = static_cast<u32>(indexBuffer.indexCount()),
      .instanceCount = 1,
  });
  return input;
}

inline LX_core::PipelineBuildDesc
makeMinimalDirectRasterHelperPipelineBuildDescForVulkanTests(
    const LX_core::IVertexBuffer &vertexBuffer,
    const LX_core::IndexBuffer &indexBuffer,
    LX_core::StringID pass = LX_core::Pass_PostProcess,
    const LX_core::RenderTarget &target = {}) {
  constexpr const char *kShaderName = "minimal";
  auto shader = makeMinimalShaderForVulkanTests();

  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kShaderName;
  shaderProgram.shader = shader;

  LX_core::RenderState renderState;
  renderState.cullMode = LX_core::CullMode::None;
  renderState.depthTestEnable = false;
  renderState.depthWriteEnable = false;

  const LX_core::PipelineKey key = LX_core::PipelineKey::build(
      shaderProgram.getPipelineSignature(),
      testRenderPathNodeSignature(pass, target));
  return LX_core::PipelineBuildDesc::graphics(
      key, shaderProgram.getPipelineSignature(), target.toDesc(),
      shader->getAllStages(), shader->getReflectionBindings(),
      vertexBuffer.getLayout(), renderState, indexBuffer.getTopology(),
      std::nullopt, {});
}

inline LX_core::PipelineBuildDesc
makeFullscreenTrianglePipelineBuildDescForVulkanTests(
    LX_core::StringID pass = LX_core::Pass_PostProcess,
    const LX_core::RenderTarget &target = {}) {
  constexpr const char *kShaderName = "ibl_brdf_lut";
  std::vector<LX_core::ShaderStageCode> stages{
      loadTestShaderStage(kShaderName, "vert.spv", LX_core::ShaderStage::Vertex),
      loadTestShaderStage(kShaderName, "frag.spv",
                          LX_core::ShaderStage::Fragment),
  };
  auto shader = std::make_shared<LX_infra::CompiledShader>(
      stages, LX_infra::ShaderReflector::reflect(stages),
      LX_infra::ShaderReflector::reflectVertexInputs(stages), kShaderName);

  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kShaderName;
  shaderProgram.shader = shader;

  LX_core::RenderState renderState;
  renderState.cullMode = LX_core::CullMode::None;
  renderState.depthTestEnable = false;
  renderState.depthWriteEnable = false;

  const LX_core::PipelineKey key = LX_core::PipelineKey::build(
      shaderProgram.getPipelineSignature(),
      testRenderPathNodeSignature(pass, target));
  return LX_core::PipelineBuildDesc::graphics(
      key, shaderProgram.getPipelineSignature(), target.toDesc(),
      shader->getAllStages(), shader->getReflectionBindings(),
      LX_core::VertexLayout{}, renderState,
      LX_core::PrimitiveTopology::TriangleList, std::nullopt, {});
}

inline LX_core::RenderInputDesc makeAcceptedRenderInputDescForVulkanTests(
    const LX_core::PipelineBuildDesc &pipelineDesc,
    const LX_core::RenderDrawInput &input) {
  LX_core::RenderInputDesc desc;
  desc.status = LX_core::RenderInputStatus::Accepted;
  desc.inputIndex = input.inputIndex;
  desc.pass = input.pass;
  desc.debugId = input.debugId;
  desc.pipelineKey = pipelineDesc.key;
  desc.pipelineBuildDesc = pipelineDesc;
  desc.shaderVariantKey = pipelineDesc.shaderVariantKey;
  desc.resourceDependencies.push_back(input.vertexBuffer);
  desc.resourceDependencies.push_back(input.indexBuffer);
  return desc;
}

inline LX_core::MaterialInstanceSharedPtr
makeForwardMinimalMaterialForVulkanTests() {
  constexpr const char *kShaderName = "minimal";
  std::vector<LX_core::ShaderStageCode> stages{
      loadTestShaderStage(kShaderName, "vert.spv", LX_core::ShaderStage::Vertex),
      loadTestShaderStage(kShaderName, "frag.spv",
                          LX_core::ShaderStage::Fragment),
  };

  auto shader = std::make_shared<LX_infra::CompiledShader>(
      stages, LX_infra::ShaderReflector::reflect(stages),
      LX_infra::ShaderReflector::reflectVertexInputs(stages), kShaderName);

  auto materialTemplate = LX_core::MaterialTemplate::create("vulkan_test_pbr");
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kShaderName;
  shaderProgram.shader = std::move(shader);

  LX_core::MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderProgram);
  passDefinition.renderState = LX_core::RenderState{};
  passDefinition.renderState.cullMode = LX_core::CullMode::Back;
  passDefinition.renderState.depthTestEnable = true;
  passDefinition.renderState.depthWriteEnable = true;
  passDefinition.renderState.depthOp = LX_core::CompareOp::LessEqual;
  passDefinition.renderState.blendEnable = false;
  materialTemplate->setPassDefinition(LX_core::Pass_Forward,
                                      std::move(passDefinition));
  materialTemplate->rebuildMaterialInterface();

  auto material = LX_core::MaterialInstance::create(materialTemplate);
  material->setBsdfType("uber");
  material->syncGpuData();
  return material;
}

} // namespace LX_test
