#include "infra/loaders/blinnphong_draw_material_loader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_impl.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"
#include <filesystem>
#include <stdexcept>

namespace LX_infra {

namespace fs = std::filesystem;

LX_core::DrawMaterial::Ptr
loadBlinnPhongDrawMaterial(LX_core::ResourcePassFlag passFlag) {
  const std::string baseName = "blinnphong_0";
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
    if (parent == cwd) {
      break;
    }
    cwd = parent;
  }
  if (glslDir.empty()) {
    throw std::runtime_error(
        "blinnphong GLSL sources not found (expected .../shaders/glsl/)");
  }
  fs::path vert = glslDir / (baseName + ".vert");
  fs::path frag = glslDir / (baseName + ".frag");

  auto compiled = ShaderCompiler::compileProgram(vert, frag, {});
  if (!compiled.success) {
    throw std::runtime_error("blinnphong compile failed: " + compiled.errorMessage);
  }

  auto bindings = ShaderReflector::reflect(compiled.stages);
  auto shader = std::make_shared<ShaderImpl>(std::move(compiled.stages), bindings,
                                             baseName);

  LX_core::ShaderProgramSet set;
  set.shaderName = baseName;
  return LX_core::DrawMaterial::create(shader, set, passFlag);
}

} // namespace LX_infra
