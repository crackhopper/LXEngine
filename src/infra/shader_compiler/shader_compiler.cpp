#include "shader_compiler.hpp"
#include <fstream>
#include <iostream>
#include <shaderc/shaderc.hpp>
#include <sstream>

namespace LX_infra {

static std::string readFileToString(const std::filesystem::path &path) {
  std::ifstream ifs(path, std::ios::in);
  if (!ifs.is_open()) {
    return {};
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

static shaderc_shader_kind
toShadercKind(LX_core::ShaderStage stage) {
  switch (stage) {
  case LX_core::ShaderStage::Vertex:
    return shaderc_vertex_shader;
  case LX_core::ShaderStage::Fragment:
    return shaderc_fragment_shader;
  case LX_core::ShaderStage::Compute:
    return shaderc_compute_shader;
  case LX_core::ShaderStage::Geometry:
    return shaderc_geometry_shader;
  case LX_core::ShaderStage::TessControl:
    return shaderc_tess_control_shader;
  case LX_core::ShaderStage::TessEval:
    return shaderc_tess_evaluation_shader;
  default:
    return shaderc_glsl_infer_from_source;
  }
}

LX_core::ShaderStage
ShaderCompiler::deduceStageFromExtension(const std::filesystem::path &path) {
  auto ext = path.extension().string();
  if (ext == ".vert")
    return LX_core::ShaderStage::Vertex;
  if (ext == ".frag")
    return LX_core::ShaderStage::Fragment;
  if (ext == ".comp")
    return LX_core::ShaderStage::Compute;
  if (ext == ".geom")
    return LX_core::ShaderStage::Geometry;
  if (ext == ".tesc")
    return LX_core::ShaderStage::TessControl;
  if (ext == ".tese")
    return LX_core::ShaderStage::TessEval;
  return LX_core::ShaderStage::None;
}

CompileResult
ShaderCompiler::compileFile(const std::filesystem::path &filePath,
                            const std::vector<LX_core::ShaderVariant> &variants) {
  CompileResult result;

  // Read source
  std::string source = readFileToString(filePath);
  if (source.empty()) {
    result.errorMessage =
        "Failed to read shader file: " + filePath.string();
    return result;
  }

  // Deduce stage
  auto stage = deduceStageFromExtension(filePath);
  if (stage == LX_core::ShaderStage::None) {
    result.errorMessage =
        "Cannot deduce shader stage from extension: " + filePath.string();
    return result;
  }

  // Setup shaderc compiler + options
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetTargetEnvironment(shaderc_target_env_vulkan,
                               shaderc_env_version_vulkan_1_3);
  options.SetOptimizationLevel(shaderc_optimization_level_zero);

  // Inject variant macros
  for (const auto &v : variants) {
    if (v.enabled) {
      options.AddMacroDefinition(v.macroName, "1");
    }
  }

  // Compile
  auto module = compiler.CompileGlslToSpv(
      source, toShadercKind(stage), filePath.filename().string().c_str(),
      options);

  if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
    result.errorMessage = module.GetErrorMessage();
    return result;
  }

  // Extract bytecode
  LX_core::ShaderStageCode stageCode;
  stageCode.stage = stage;
  stageCode.bytecode.assign(module.cbegin(), module.cend());

  result.success = true;
  result.stages.push_back(std::move(stageCode));
  return result;
}

CompileResult ShaderCompiler::compileProgram(
    const std::filesystem::path &vertPath,
    const std::filesystem::path &fragPath,
    const std::vector<LX_core::ShaderVariant> &variants) {

  auto vertResult = compileFile(vertPath, variants);
  if (!vertResult.success) {
    return vertResult;
  }

  auto fragResult = compileFile(fragPath, variants);
  if (!fragResult.success) {
    return fragResult;
  }

  CompileResult result;
  result.success = true;
  result.stages = std::move(vertResult.stages);
  result.stages.insert(result.stages.end(),
                       std::make_move_iterator(fragResult.stages.begin()),
                       std::make_move_iterator(fragResult.stages.end()));
  return result;
}

} // namespace LX_infra
