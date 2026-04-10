#pragma once
#include "core/resources/shader.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace LX_infra {

struct CompileResult {
  bool success = false;
  std::string errorMessage;
  std::vector<LX_core::ShaderStageCode> stages;
};

class ShaderCompiler {
public:
  /// Compile a single GLSL file to SPIR-V.
  /// @param filePath  Path to .vert / .frag file
  /// @param variants  Enabled variants become #define macros
  /// @return CompileResult with bytecode or error
  static CompileResult
  compileFile(const std::filesystem::path &filePath,
              const std::vector<LX_core::ShaderVariant> &variants = {});

  /// Compile multiple GLSL stage files for a single shader program.
  /// @param vertPath  Vertex shader source path
  /// @param fragPath  Fragment shader source path
  /// @param variants  Variant macros
  static CompileResult
  compileProgram(const std::filesystem::path &vertPath,
                 const std::filesystem::path &fragPath,
                 const std::vector<LX_core::ShaderVariant> &variants = {});

private:
  static LX_core::ShaderStage
  deduceStageFromExtension(const std::filesystem::path &path);
};

} // namespace LX_infra
