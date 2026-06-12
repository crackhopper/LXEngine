#pragma once

// Shared helpers for integration tests that need to materialize a
// RenderWorkItem from a Scene. Originally REQ-008; REQ-009 adds a target
// parameter so the queue's scene-level-resource filter can match the
// camera's RenderTarget.

#include "core/rhi/gpu_resource.hpp"
#include "core/asset/material_instance.hpp"
#include "core/asset/material_pass_definition.hpp"
#include "core/asset/material_template.hpp"
#include "core/asset/shader.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_test {

/// Build a local RenderWorkQueue from `scene` for `pass` + `target` and return
/// the first RenderWorkItem. Asserts the queue is non-empty. Default
inline LX_core::RenderWorkItem
firstItemFromScene(LX_core::Scene &scene, LX_core::StringID pass,
                   const LX_core::RenderTarget &target = {}) {
  LX_core::RenderWorkQueue q;
  q.build(LX_core::RenderWorkBuildContext::realtime(scene), pass, target);
  assert(!q.getItems().empty() &&
         "scene produced no items for pass/target");
  return q.getItems().front();
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
