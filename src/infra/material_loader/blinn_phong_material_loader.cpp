#include "infra/material_loader/blinn_phong_material_loader.hpp"
#include "core/frame_graph/pass.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace LX_infra {

namespace fs = std::filesystem;

LX_core::MaterialInstance::Ptr
loadBlinnPhongMaterial(LX_core::ResourcePassFlag passFlag) {
  const std::string baseName = "blinnphong_0";

  // Walk up from the current working directory to locate shaders/glsl.
  fs::path cwd = fs::current_path();
  fs::path glslDir;
  for (int i = 0; i < 4; ++i) {
    fs::path candidate = cwd / "shaders" / "glsl";
    if (fs::exists(candidate / (baseName + ".vert")) &&
        fs::exists(candidate / (baseName + ".frag"))) {
      glslDir = std::move(candidate);
      break;
    }
    fs::path parent = cwd.parent_path();
    if (parent == cwd)
      break;
    cwd = parent;
  }
  if (glslDir.empty()) {
    throw std::runtime_error(
        "blinnphong GLSL sources not found (expected .../shaders/glsl/)");
  }

  const fs::path vert = glslDir / (baseName + ".vert");
  const fs::path frag = glslDir / (baseName + ".frag");

  auto compiled = ShaderCompiler::compileProgram(vert, frag, {});
  if (!compiled.success) {
    throw std::runtime_error("blinnphong compile failed: " +
                             compiled.errorMessage);
  }

  auto bindings = ShaderReflector::reflect(compiled.stages);
  auto shader = std::make_shared<CompiledShader>(std::move(compiled.stages),
                                             bindings, baseName);

  auto tmpl = LX_core::MaterialTemplate::create(baseName, shader);

  LX_core::ShaderProgramSet programSet;
  programSet.shaderName = baseName;

  LX_core::RenderPassEntry entry;
  entry.shaderSet = programSet;
  entry.renderState = LX_core::RenderState{};
  entry.buildCache(); // populates per-entry bindingCache for backend lookups
  tmpl->setPass(LX_core::Pass_Forward, std::move(entry));
  tmpl->buildBindingCache();

  auto mat = LX_core::MaterialInstance::create(tmpl, passFlag);

  // Seed default uniform values matching the previous BlinnPhongMaterialUBO
  // defaults. Member names come from the GLSL declaration in
  // `shaders/glsl/blinnphong_0.frag::MaterialUBO`.
  mat->setVec3(LX_core::StringID("baseColor"),
               LX_core::Vec3f{0.8f, 0.8f, 0.8f});
  mat->setFloat(LX_core::StringID("shininess"), 12.0f);
  mat->setFloat(LX_core::StringID("specularIntensity"), 1.0f);
  mat->setInt(LX_core::StringID("enableAlbedo"), 0);
  mat->setInt(LX_core::StringID("enableNormal"), 0);
  mat->updateUBO();

  return mat;
}

} // namespace LX_infra
