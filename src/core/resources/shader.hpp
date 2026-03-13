#pragma once
#include "../gpu/render_resource.hpp"
#include "../platform/types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace LX_core {

class Shader : public IRenderResource {
public:
  Shader(const std::string &shaderName, ResourcePassFlag passFlag)
      : m_shaderName(shaderName), m_passFlag(passFlag) {}

  virtual ResourcePassFlag getPassFlag() const override { return m_passFlag; }
  virtual const void *getRawData() const override {
    return nullptr;
  }
  virtual u32 getByteSize() const override {
    return 0;
  }

  std::string getShaderName() const { return m_shaderName; }

private:
  std::string m_shaderName;
  ResourcePassFlag m_passFlag = ResourcePassFlag::Forward;
};
using ShaderPtr = std::shared_ptr<Shader>;


class VertexShader : public Shader {
public:
  VertexShader(const std::string &shaderName, ResourcePassFlag passFlag)
      : Shader(shaderName, passFlag) {}

  virtual ResourceType getType() const override {
    return ResourceType::VertexShader;
  }      
};

class FragmentShader : public Shader {
public:
  FragmentShader(const std::string &shaderName, ResourcePassFlag passFlag)
      : Shader(shaderName, passFlag) {}

  virtual ResourceType getType() const override {
    return ResourceType::FragmentShader;
  }      
};


} // namespace LX_core