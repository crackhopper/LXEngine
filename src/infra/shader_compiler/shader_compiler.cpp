#include "shader_compiler.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <shaderc/shaderc.hpp>
#include <sstream>
#include <utility>

namespace LX_infra {
namespace {

static std::string readFileToString(const std::filesystem::path &path) {
  std::ifstream ifs(path, std::ios::in);
  if (!ifs.is_open()) {
    return {};
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

static std::filesystem::path
findShaderIncludeRoot(const std::filesystem::path &filePath) {
  std::filesystem::path probe = filePath.parent_path();
  for (int i = 0; i < 6; ++i) {
    if (std::filesystem::exists(probe / "common")) {
      return probe;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  return filePath.parent_path();
}

class FileIncluder final : public shaderc::CompileOptions::IncluderInterface {
public:
  explicit FileIncluder(std::filesystem::path includeRoot)
      : m_includeRoot(std::move(includeRoot)) {}

  shaderc_include_result *
  GetInclude(const char *requestedSource, shaderc_include_type type,
             const char *requestingSource, usize) override {
    auto include = std::make_unique<IncludeResult>();
    const std::filesystem::path requestedPath(requestedSource);
    const std::filesystem::path requesterPath(requestingSource);
    const std::filesystem::path baseDir =
        type == shaderc_include_type_relative &&
                !requesterPath.parent_path().empty()
            ? requesterPath.parent_path()
            : m_includeRoot;
    const std::filesystem::path fullPath =
        requestedPath.is_absolute() ? requestedPath : baseDir / requestedPath;

    std::filesystem::path resolvedPath = fullPath.lexically_normal();
    include->content = readFileToString(resolvedPath);
    if (include->content.empty() && !requestedPath.is_absolute() &&
        resolvedPath.parent_path() != m_includeRoot) {
      resolvedPath = (m_includeRoot / requestedPath).lexically_normal();
      include->content = readFileToString(resolvedPath);
    }
    include->sourceName = resolvedPath.string();
    if (include->content.empty()) {
      include->content =
          "#error failed to read shader include: " + include->sourceName;
      include->result.source_name = include->sourceName.c_str();
      include->result.source_name_length = include->sourceName.size();
      include->result.content = include->content.c_str();
      include->result.content_length = include->content.size();
      include->result.user_data = include.get();
      return &include.release()->result;
    }

    include->result.source_name = include->sourceName.c_str();
    include->result.source_name_length = include->sourceName.size();
    include->result.content = include->content.c_str();
    include->result.content_length = include->content.size();
    include->result.user_data = include.get();
    return &include.release()->result;
  }

  void ReleaseInclude(shaderc_include_result *data) override {
    delete static_cast<IncludeResult *>(data->user_data);
  }

private:
  struct IncludeResult final {
    std::string sourceName;
    std::string content;
    shaderc_include_result result{};
  };

  std::filesystem::path m_includeRoot;
};

} // namespace

static shaderc_shader_kind toShadercKind(LX_core::ShaderStage stage) {
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
  options.SetIncluder(
      std::make_unique<FileIncluder>(findShaderIncludeRoot(filePath)));

  // Inject variant macros
  for (const auto &v : variants) {
    if (v.enabled) {
      options.AddMacroDefinition(v.macroName, "1");
    }
  }

  // Compile
  auto module = compiler.CompileGlslToSpv(
      source, toShadercKind(stage), filePath.string().c_str(),
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
