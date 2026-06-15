#include "vulkan_post_process_builder.hpp"

#include "core/asset/material_pass_definition.hpp"
#include "core/asset/material_template.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/ibl_environment.hpp"
#include "core/scene/light.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "core/utils/hash.hpp"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_core::backend {
namespace {

constexpr const char *kPostProcessShaderName = "render_paths/Post/post_process";
constexpr const char *kBloomThresholdShaderName =
    "render_paths/Post/bloom_threshold";
constexpr const char *kSkyboxShaderName = "skybox";
constexpr const char *kDeferredLightingShaderName =
    "render_paths/Deferred/deferred_lighting";

class StaticFullscreenShader final : public LX_core::IShader {
public:
  StaticFullscreenShader(std::string shaderName,
                         std::vector<LX_core::ShaderStageCode> stages,
                         std::vector<LX_core::ShaderResourceBinding> bindings)
      : m_shaderName(std::move(shaderName)), m_stages(std::move(stages)),
        m_bindings(std::move(bindings)) {}

  const std::vector<LX_core::ShaderStageCode> &getAllStages() const override {
    return m_stages;
  }

  const std::vector<LX_core::ShaderResourceBinding> &
  getReflectionBindings() const override {
    return m_bindings;
  }

  std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
  findBinding(u32 set, u32 binding) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.set == set && candidate.binding == binding) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  std::optional<std::reference_wrapper<const LX_core::ShaderResourceBinding>>
  findBinding(const std::string &name) const override {
    for (const auto &candidate : m_bindings) {
      if (candidate.name == name) {
        return std::cref(candidate);
      }
    }
    return std::nullopt;
  }

  usize getProgramHash() const override {
    usize hash = 0;
    for (const auto &stage : m_stages) {
      LX_core::hash_combine(hash, static_cast<u32>(stage.stage));
      LX_core::hash_combine(hash, stage.bytecode.size());
      for (const auto word : stage.bytecode) {
        LX_core::hash_combine(hash, word);
      }
    }
    return hash;
  }

  std::string getShaderName() const override { return m_shaderName; }

private:
  std::string m_shaderName;
  std::vector<LX_core::ShaderStageCode> m_stages;
  std::vector<LX_core::ShaderResourceBinding> m_bindings;
};

std::vector<LX_core::ShaderResourceBinding> postProcessBindings() {
  return {
      LX_core::ShaderResourceBinding{"SceneColor",
                                     0,
                                     0,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{
          "PostProcessUBO",
          0,
          1,
          LX_core::ShaderPropertyType::UniformBuffer,
          1,
          16,
          0,
          LX_core::ShaderStage::Fragment,
          {LX_core::StructMemberInfo{"exposure",
                                     LX_core::ShaderPropertyType::Float, 0, 4},
           LX_core::StructMemberInfo{"toneMappingMode",
                                     LX_core::ShaderPropertyType::Int, 4, 4},
           LX_core::StructMemberInfo{"gamma",
                                     LX_core::ShaderPropertyType::Float, 8, 4},
           LX_core::StructMemberInfo{
               "bloomIntensity", LX_core::ShaderPropertyType::Float, 12, 4}}},
      LX_core::ShaderResourceBinding{"BloomColor",
                                     0,
                                     2,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
  };
}

std::vector<LX_core::ShaderResourceBinding> skyboxBindings() {
  return {
      LX_core::ShaderResourceBinding{
          "CameraUBO",
          0,
          0,
          LX_core::ShaderPropertyType::UniformBuffer,
          1,
          LX_core::CameraData::ResourceSize,
          0,
          LX_core::ShaderStage::Vertex | LX_core::ShaderStage::Fragment,
          {LX_core::StructMemberInfo{"view", LX_core::ShaderPropertyType::Mat4,
                                     0, 64},
           LX_core::StructMemberInfo{"proj", LX_core::ShaderPropertyType::Mat4,
                                     64, 64},
           LX_core::StructMemberInfo{
               "eyePos", LX_core::ShaderPropertyType::Vec3, 128, 12}}},
      LX_core::ShaderResourceBinding{"SkyboxMap",
                                     1,
                                     0,
                                     LX_core::ShaderPropertyType::TextureCube,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{
          "EnvironmentUBO",
          2,
          0,
          LX_core::ShaderPropertyType::UniformBuffer,
          1,
          sizeof(LX_core::EnvironmentData::Param),
          0,
          LX_core::ShaderStage::Fragment,
          {LX_core::StructMemberInfo{
              "params", LX_core::ShaderPropertyType::Vec4, 0, 16}}},
  };
}

std::vector<LX_core::ShaderResourceBinding> bloomThresholdBindings() {
  return {
      LX_core::ShaderResourceBinding{"SceneColor",
                                     0,
                                     0,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{
          "BloomThresholdUBO",
          0,
          1,
          LX_core::ShaderPropertyType::UniformBuffer,
          1,
          16,
          0,
          LX_core::ShaderStage::Fragment,
          {LX_core::StructMemberInfo{"threshold",
                                     LX_core::ShaderPropertyType::Float, 0, 4},
           LX_core::StructMemberInfo{"softKnee",
                                     LX_core::ShaderPropertyType::Float, 4, 4},
           LX_core::StructMemberInfo{"enabled",
                                     LX_core::ShaderPropertyType::Float, 8, 4},
           LX_core::StructMemberInfo{
               "padding", LX_core::ShaderPropertyType::Float, 12, 4}}},
  };
}

std::vector<LX_core::ShaderResourceBinding> bloomBlurBindings() {
  return {LX_core::ShaderResourceBinding{"BloomSource",
                                         0,
                                         0,
                                         LX_core::ShaderPropertyType::Texture2D,
                                         1,
                                         0,
                                         0,
                                         LX_core::ShaderStage::Fragment,
                                         {}}};
}

std::vector<LX_core::ShaderResourceBinding> deferredLightingBindings() {
  return {
      LX_core::ShaderResourceBinding{"GBufferAlbedoAlpha",
                                     0,
                                     0,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{"GBufferNormalRoughness",
                                     0,
                                     1,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{"GBufferMaterial",
                                     0,
                                     2,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{"GBufferDepth",
                                     0,
                                     3,
                                     LX_core::ShaderPropertyType::Texture2D,
                                     1,
                                     0,
                                     0,
                                     LX_core::ShaderStage::Fragment,
                                     {}},
      LX_core::ShaderResourceBinding{
          "CameraUBO",
          1,
          0,
          LX_core::ShaderPropertyType::UniformBuffer,
          1,
          LX_core::CameraData::ResourceSize,
          0,
          LX_core::ShaderStage::Fragment,
          {LX_core::StructMemberInfo{"view", LX_core::ShaderPropertyType::Mat4,
                                     0, 64},
           LX_core::StructMemberInfo{"proj", LX_core::ShaderPropertyType::Mat4,
                                     64, 64},
           LX_core::StructMemberInfo{
               "eyePos", LX_core::ShaderPropertyType::Vec3, 128, 12}}},
      LX_core::ShaderResourceBinding{
          "LightUBO",
          2,
          0,
          LX_core::ShaderPropertyType::UniformBuffer,
          1,
          sizeof(LX_core::DirectionalLightData::Param),
          0,
          LX_core::ShaderStage::Fragment,
          {LX_core::StructMemberInfo{"direction",
                                     LX_core::ShaderPropertyType::Vec4, 0, 16},
           LX_core::StructMemberInfo{"color", LX_core::ShaderPropertyType::Vec4,
                                     16, 16}}},
  };
}

LX_core::ShaderStageCode loadShaderStage(const std::string &shaderName,
                                         const char *suffix,
                                         LX_core::ShaderStage stage) {
  const auto bytes = readFile(
      (getRuntimeShaderBinaryDir() / (shaderName + "." + suffix)).string());
  if ((bytes.size() % sizeof(u32)) != 0) {
    throw std::runtime_error("shader bytecode size is not 4-byte aligned: " +
                             shaderName + "." + suffix);
  }

  LX_core::ShaderStageCode code;
  code.stage = stage;
  code.bytecode.resize(bytes.size() / sizeof(u32));
  std::memcpy(code.bytecode.data(), bytes.data(), bytes.size());
  return code;
}

std::vector<LX_core::ShaderStageCode>
loadGraphicsShaderStages(const std::string &shaderName) {
  return {
      loadShaderStage(shaderName, "vert.spv", LX_core::ShaderStage::Vertex),
      loadShaderStage(shaderName, "frag.spv", LX_core::ShaderStage::Fragment)};
}

LX_core::MaterialPassDefinition
makeFullscreenPassDefinition(LX_core::ShaderProgramSet shaderProgram) {
  LX_core::MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderProgram);
  passDefinition.renderState.cullMode = LX_core::CullMode::None;
  passDefinition.renderState.depthTestEnable = false;
  passDefinition.renderState.depthWriteEnable = false;
  passDefinition.renderState.blendEnable = false;
  return passDefinition;
}

} // namespace

VulkanPostProcessBuilder::VulkanPostProcessBuilder(
    const VulkanPostProcessSettings &settings)
    : m_settings(settings) {}

LX_core::MaterialInstanceUniquePtr
VulkanPostProcessBuilder::createStandardPostProcessMaterial() const {
  auto shader = std::make_shared<StaticFullscreenShader>(
      kPostProcessShaderName, loadGraphicsShaderStages(kPostProcessShaderName),
      postProcessBindings());

  auto tmpl = LX_core::MaterialTemplate::create(kPostProcessShaderName);
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kPostProcessShaderName;
  shaderProgram.shader = shader;
  tmpl->setPassDefinition(LX_core::Pass_PostProcess,
                          makeFullscreenPassDefinition(shaderProgram));
  tmpl->rebuildMaterialInterface();

  auto material = LX_core::MaterialInstance::createUnique(std::move(tmpl));
  material->writeShaderBindingParameter(LX_core::StringID("PostProcessUBO"),
                                        LX_core::StringID("exposure"), 1.0f);
  material->writeShaderBindingParameter(LX_core::StringID("PostProcessUBO"),
                                        LX_core::StringID("toneMappingMode"),
                                        0);
  material->writeShaderBindingParameter(LX_core::StringID("PostProcessUBO"),
                                        LX_core::StringID("gamma"), 2.2f);
  material->writeShaderBindingParameter(
      LX_core::StringID("PostProcessUBO"), LX_core::StringID("bloomIntensity"),
      m_settings.bloomEnabled ? m_settings.bloomIntensity : 0.0f);
  material->syncGpuData();
  return material;
}

LX_core::MaterialInstanceUniquePtr
VulkanPostProcessBuilder::createBloomThresholdMaterial() const {
  auto shader = std::make_shared<StaticFullscreenShader>(
      kBloomThresholdShaderName,
      loadGraphicsShaderStages(kBloomThresholdShaderName),
      bloomThresholdBindings());

  auto tmpl = LX_core::MaterialTemplate::create(kBloomThresholdShaderName);
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kBloomThresholdShaderName;
  shaderProgram.shader = shader;
  tmpl->setPassDefinition(LX_core::Pass_BloomThreshold,
                          makeFullscreenPassDefinition(shaderProgram));
  tmpl->rebuildMaterialInterface();

  auto material = LX_core::MaterialInstance::createUnique(std::move(tmpl));
  material->writeShaderBindingParameter(LX_core::StringID("BloomThresholdUBO"),
                                        LX_core::StringID("threshold"),
                                        m_settings.bloomThreshold);
  material->writeShaderBindingParameter(LX_core::StringID("BloomThresholdUBO"),
                                        LX_core::StringID("softKnee"),
                                        m_settings.bloomSoftKnee);
  material->writeShaderBindingParameter(LX_core::StringID("BloomThresholdUBO"),
                                        LX_core::StringID("enabled"), 1.0f);
  material->writeShaderBindingParameter(LX_core::StringID("BloomThresholdUBO"),
                                        LX_core::StringID("padding"), 0.0f);
  material->syncGpuData();
  return material;
}

LX_core::MaterialInstanceUniquePtr
VulkanPostProcessBuilder::createBloomBlurMaterial(
    LX_core::StringID pass, const char *shaderName) const {
  auto shader = std::make_shared<StaticFullscreenShader>(
      shaderName, loadGraphicsShaderStages(shaderName), bloomBlurBindings());

  auto tmpl = LX_core::MaterialTemplate::create(shaderName);
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = shaderName;
  shaderProgram.shader = shader;
  tmpl->setPassDefinition(pass, makeFullscreenPassDefinition(shaderProgram));
  tmpl->rebuildMaterialInterface();
  auto material = LX_core::MaterialInstance::createUnique(std::move(tmpl));
  material->syncGpuData();
  return material;
}

LX_core::MaterialInstanceUniquePtr
VulkanPostProcessBuilder::createDeferredLightingMaterial() const {
  auto shader = std::make_shared<StaticFullscreenShader>(
      kDeferredLightingShaderName,
      loadGraphicsShaderStages(kDeferredLightingShaderName),
      deferredLightingBindings());

  auto tmpl = LX_core::MaterialTemplate::create(kDeferredLightingShaderName);
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kDeferredLightingShaderName;
  shaderProgram.shader = shader;
  tmpl->setPassDefinition(LX_core::Pass_DeferredLighting,
                          makeFullscreenPassDefinition(shaderProgram));
  tmpl->rebuildMaterialInterface();
  auto material = LX_core::MaterialInstance::createUnique(std::move(tmpl));
  material->syncGpuData();
  return material;
}

LX_core::MaterialInstanceUniquePtr
VulkanPostProcessBuilder::createSkyboxBackgroundMaterial() const {
  auto shader = std::make_shared<StaticFullscreenShader>(
      kSkyboxShaderName, loadGraphicsShaderStages(kSkyboxShaderName),
      skyboxBindings());

  auto tmpl = LX_core::MaterialTemplate::create(kSkyboxShaderName);
  LX_core::ShaderProgramSet shaderProgram;
  shaderProgram.shaderName = kSkyboxShaderName;
  shaderProgram.shader = shader;

  LX_core::MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram = std::move(shaderProgram);
  passDefinition.renderState.cullMode = LX_core::CullMode::None;
  passDefinition.renderState.depthTestEnable = true;
  passDefinition.renderState.depthWriteEnable = false;
  passDefinition.renderState.depthOp = LX_core::CompareOp::LessEqual;
  passDefinition.renderState.blendEnable = false;
  tmpl->setPassDefinition(LX_core::Pass_Forward, std::move(passDefinition));
  tmpl->rebuildMaterialInterface();

  auto material = LX_core::MaterialInstance::createUnique(std::move(tmpl));
  material->syncGpuData();
  return material;
}

} // namespace LX_core::backend
