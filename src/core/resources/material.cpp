#include "material.hpp"
#include <string>

namespace LX_core {

DrawMaterial::Ptr DrawMaterial::create(IShaderPtr shader, ShaderProgramSet programSet,
                                     ResourcePassFlag passFlag) {
  auto ubo = std::make_shared<BlinnPhongMaterialUBO>(passFlag);
  ubo->params.enableAlbedo = 0;
  ubo->params.enableNormalMap = 0;
  return Ptr(new DrawMaterial(std::move(shader), std::move(programSet), RenderState{},
                              passFlag, std::move(ubo)));
}

DrawMaterial::DrawMaterial(IShaderPtr shader, ShaderProgramSet programSet,
                           RenderState renderState, ResourcePassFlag passFlag,
                           BlinnPhongMaterialUboPtr ubo)
    : ubo(std::move(ubo)), m_shader(std::move(shader)),
      m_programSet(std::move(programSet)), m_renderState(renderState),
      m_passFlag(passFlag) {}

std::vector<IRenderResourcePtr> DrawMaterial::getDescriptorResources() const {
  std::vector<IRenderResourcePtr> out;
  out.push_back(std::dynamic_pointer_cast<IRenderResource>(ubo));
  if (albedoSampler) {
    out.push_back(std::dynamic_pointer_cast<IRenderResource>(albedoSampler));
  }
  if (normalSampler) {
    out.push_back(std::dynamic_pointer_cast<IRenderResource>(normalSampler));
  }
  return out;
}

IShaderPtr DrawMaterial::getShaderInfo() const { return m_shader; }

ResourcePassFlag DrawMaterial::getPassFlag() const { return m_passFlag; }

ShaderProgramSet DrawMaterial::getShaderProgramSet() const { return m_programSet; }

RenderState DrawMaterial::getRenderState() const { return m_renderState; }

} // namespace LX_core
