#include "infra/resource_parsers/material_source_variant_resolver.hpp"

#include "infra/resource_parsers/render_path_shader_resolver.hpp"
#include "infra/shader_compiler/compiled_shader.hpp"
#include "infra/shader_compiler/shader_compiler.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include "core/asset/material_instance.hpp"
#include "core/asset/material_template.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace LX_infra {
namespace {

[[nodiscard]] std::filesystem::path
pathFromUri(const LX_core::ResourceUri &uri) {
  const std::string &text = uri.string();
  constexpr std::string_view assetsPrefix = "assets://";
  constexpr std::string_view filePrefix = "file://";
  if (text.rfind(assetsPrefix, 0) == 0) {
    return std::filesystem::path("assets") / text.substr(assetsPrefix.size());
  }
  if (text.rfind(filePrefix, 0) == 0) {
    return std::filesystem::path(text.substr(filePrefix.size()));
  }
  return std::filesystem::path(text);
}

[[nodiscard]] std::string signatureDebug(LX_core::StringID signature) {
  return signature.id == 0
             ? std::string("<none>")
             : LX_core::GlobalStringTable::get().toDebugString(signature);
}

[[nodiscard]] std::string materialDebug(
    LX_core::MaterialHandle handle, const LX_core::ResourceUri &uri,
    const LX_core::MaterialInstance &material) {
  std::ostringstream oss;
  oss << "materialUri=" << (uri.empty() ? "<anonymous>" : uri.string())
      << " handle=" << handle.index << ":" << handle.generation
      << " type=" << material.getBsdfType()
      << " source=" << material.getMaterialSourceUri().string()
      << " reflectionHash=" << material.getMaterialSourceReflectionHash()
      << " sourceSignature="
      << signatureDebug(material.getMaterialSourceSignature());
  return oss.str();
}

struct MaterialEntry final {
  LX_core::MaterialHandle handle;
  LX_core::ResourceUri uri;
  LX_core::MaterialInstance *material = nullptr;
};

struct TypeSourceFacts final {
  std::string type;
  LX_core::ResourceUri sourceUri;
  std::string reflectionHash;
  LX_core::StringID sourceSignature;
  std::vector<MaterialEntry> materials;
};

[[nodiscard]] bool passRequiresMaterialBsdf(const LX_core::RenderPassNode &pass) {
  return std::find(pass.sources.begin(), pass.sources.end(), "material.bsdf") !=
         pass.sources.end();
}

[[nodiscard]] bool graphMaterialBsdfPassAllowsType(
    const LX_core::RenderPathGraph &graph, std::string_view type) {
  for (const LX_core::RenderPassNode &pass : graph.passes) {
    if (!passRequiresMaterialBsdf(pass)) {
      continue;
    }
    if (pass.input.material.types.empty()) {
      return true;
    }
    if (std::find(pass.input.material.types.begin(),
                  pass.input.material.types.end(),
                  std::string(type)) != pass.input.material.types.end()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<bool> shaderRequiresMaterialSourceVariant(
    const LX_core::ResourceUri &graphUri, const LX_core::RenderPassNode &pass,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics) {
  for (const LX_core::ResourceUri &sourceUri : sourceUris) {
    std::ifstream file(pathFromUri(sourceUri));
    if (!file) {
      diagnostics.push_back(
          "MaterialSourceVariantResolver graph=" + graphUri.string() +
          " pass=" + pass.id + " shader=" + pass.shaderUri.string() +
          " failed to inspect source=" + sourceUri.string());
      return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (buffer.str().find("LX_MATERIAL_CONTRACT_SOURCE") !=
        std::string::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool passAllowsType(const LX_core::RenderPassNode &pass,
                                  const std::string &type) {
  if (pass.input.material.types.empty()) {
    return true;
  }
  return std::find(pass.input.material.types.begin(),
                   pass.input.material.types.end(),
                   type) != pass.input.material.types.end();
}

[[nodiscard]] std::optional<LX_core::ShaderProgramSet> compileVariantShader(
    const LX_core::ResourceUri &graphUri, const LX_core::RenderPassNode &pass,
    const TypeSourceFacts &typeFacts,
    const std::vector<LX_core::ResourceUri> &sourceUris,
    std::vector<std::string> &diagnostics) {
  LX_core::ShaderVariant sourceVariant{
      .macroName = "LX_MATERIAL_CONTRACT_SOURCE",
      .enabled = true,
      .materialContractSource = typeFacts.sourceUri,
      .materialSourceSignature = typeFacts.sourceSignature,
  };
  const std::vector<LX_core::ShaderVariant> variants{sourceVariant};

  std::vector<LX_core::ShaderStageCode> stages;
  for (const LX_core::ResourceUri &sourceUri : sourceUris) {
    const auto compiled =
        ShaderCompiler::compileFile(pathFromUri(sourceUri), variants);
    if (!compiled.success) {
      diagnostics.push_back(
          "MaterialSourceVariantResolver graph=" + graphUri.string() +
          " pass=" + pass.id + " shader=" + pass.shaderUri.string() +
          " source=" + sourceUri.string() + " type=" + typeFacts.type +
          " materialSource=" + typeFacts.sourceUri.string() +
          " macro=LX_MATERIAL_CONTRACT_SOURCE compileError=" +
          compiled.errorMessage);
      return std::nullopt;
    }
    stages.insert(stages.end(),
                  std::make_move_iterator(compiled.stages.begin()),
                  std::make_move_iterator(compiled.stages.end()));
  }
  if (stages.empty()) {
    diagnostics.push_back("MaterialSourceVariantResolver graph=" +
                          graphUri.string() + " pass=" + pass.id +
                          " shader=" + pass.shaderUri.string() +
                          " type=" + typeFacts.type +
                          " produced no compiled stages");
    return std::nullopt;
  }

  std::vector<LX_core::ShaderResourceBinding> bindings;
  std::vector<LX_core::VertexInputAttribute> vertexInputs;
  try {
    bindings = ShaderReflector::reflect(stages);
    vertexInputs = ShaderReflector::reflectVertexInputs(stages);
  } catch (const std::exception &error) {
    diagnostics.push_back("MaterialSourceVariantResolver graph=" +
                          graphUri.string() + " pass=" + pass.id +
                          " shader=" + pass.shaderUri.string() +
                          " type=" + typeFacts.type +
                          " reflectionError=" + error.what());
    return std::nullopt;
  }
  if (bindings.empty() && vertexInputs.empty()) {
    diagnostics.push_back("MaterialSourceVariantResolver graph=" +
                          graphUri.string() + " pass=" + pass.id +
                          " shader=" + pass.shaderUri.string() +
                          " type=" + typeFacts.type +
                          " produced no final reflection payload");
    return std::nullopt;
  }

  LX_core::ShaderProgramSet program;
  program.shaderName = pass.shaderUri.string();
  program.variants = variants;
  program.shader = std::make_shared<CompiledShader>(
      std::move(stages), std::move(bindings), std::move(vertexInputs),
      pass.shaderUri.string());
  return program;
}

void attachVariantToMaterials(const LX_core::RenderPassNode &pass,
                              const LX_core::ShaderProgramSet &program,
                              const TypeSourceFacts &typeFacts) {
  const LX_core::StringID passId(pass.id);
  for (const MaterialEntry &entry : typeFacts.materials) {
    auto tmpl = entry.material->getTemplate();
    if (!tmpl) {
      continue;
    }
    LX_core::MaterialPassDefinition definition;
    definition.renderState = pass.renderState;
    definition.shaderProgram = program;
    tmpl->setPassDefinition(passId, std::move(definition));
    tmpl->rebuildMaterialInterface();
    entry.material->setPassEnabled(passId, true);
  }
}

} // namespace

MaterialSourceVariantResolverResult
resolveMaterialSourceVariants(LX_core::SceneResourceTable &table,
                              const LX_core::RenderPathGraph &graph,
                              const LX_core::ResourceUri &graphUri) {
  MaterialSourceVariantResolverResult result;
  result.success = false;

  std::map<std::string, TypeSourceFacts> typeFactsByType;
  table.forEachMaterialInstanceMutable(
      [&](LX_core::MaterialHandle handle, LX_core::MaterialInstance &material,
          const LX_core::ResourceUri &materialUri) {
        if (material.getBsdfType().empty()) {
          return;
        }
        if (!graphMaterialBsdfPassAllowsType(graph, material.getBsdfType())) {
          return;
        }

        const auto reflected = material.getMaterialContractReflection();
        if (!reflected.has_value()) {
          result.diagnostics.push_back(
              "MaterialSourceVariantResolver missing reflected source: " +
              materialDebug(handle, materialUri, material));
          return;
        }
        if (reflected->get().declaredType != material.getBsdfType()) {
          result.diagnostics.push_back(
              "MaterialSourceVariantResolver type/source mismatch: " +
              materialDebug(handle, materialUri, material) +
              " reflectedType=" + reflected->get().declaredType);
          return;
        }
        if (material.getMaterialSourceUri().empty() ||
            material.getMaterialSourceReflectionHash().empty() ||
            material.getMaterialSourceSignature().id == 0) {
          result.diagnostics.push_back(
              "MaterialSourceVariantResolver incomplete material source "
              "identity: " +
              materialDebug(handle, materialUri, material));
          return;
        }

        auto [it, inserted] = typeFactsByType.emplace(
            material.getBsdfType(),
            TypeSourceFacts{
                .type = material.getBsdfType(),
                .sourceUri = material.getMaterialSourceUri(),
                .reflectionHash = material.getMaterialSourceReflectionHash(),
                .sourceSignature = material.getMaterialSourceSignature(),
            });
        TypeSourceFacts &facts = it->second;
        const bool conflicts =
            facts.sourceUri != material.getMaterialSourceUri() ||
            facts.reflectionHash != material.getMaterialSourceReflectionHash() ||
            facts.sourceSignature != material.getMaterialSourceSignature();
        if (!inserted && conflicts) {
          const MaterialEntry &first = facts.materials.front();
          result.diagnostics.push_back(
              "MaterialSourceVariantResolver conflicting source identity for "
              "material type '" +
              material.getBsdfType() + "': first " +
              materialDebug(first.handle, first.uri, *first.material) +
              " conflict " + materialDebug(handle, materialUri, material));
          return;
        }
        facts.materials.push_back(MaterialEntry{
            .handle = handle,
            .uri = materialUri,
            .material = &material,
        });
      });

  if (!result.diagnostics.empty()) {
    return result;
  }
  if (typeFactsByType.empty()) {
    result.success = true;
    return result;
  }

  for (const LX_core::RenderPassNode &pass : graph.passes) {
    const auto sourceUris =
        resolveRenderPathShaderSourceUris(graphUri, pass.id, pass.shaderUri);
    if (!sourceUris.success()) {
      result.diagnostics.insert(result.diagnostics.end(),
                                sourceUris.diagnostics.begin(),
                                sourceUris.diagnostics.end());
      return result;
    }

    const std::optional<bool> requiresMaterialSourceVariant =
        shaderRequiresMaterialSourceVariant(graphUri, pass,
                                            sourceUris.sourceUris,
                                            result.diagnostics);
    if (!requiresMaterialSourceVariant.has_value()) {
      return result;
    }
    const bool declaresMaterialBsdf = passRequiresMaterialBsdf(pass);
    if (*requiresMaterialSourceVariant && !declaresMaterialBsdf) {
      result.diagnostics.push_back(
          "MaterialSourceVariantResolver graph=" + graphUri.string() +
          " pass=" + pass.id + " shader=" + pass.shaderUri.string() +
          " requires LX_MATERIAL_CONTRACT_SOURCE but the pass does not declare "
          "material.bsdf in sources");
      return result;
    }
    if (!*requiresMaterialSourceVariant && declaresMaterialBsdf) {
      result.diagnostics.push_back(
          "MaterialSourceVariantResolver graph=" + graphUri.string() +
          " pass=" + pass.id + " declares material.bsdf but shader=" +
          pass.shaderUri.string() +
          " does not include LX_MATERIAL_CONTRACT_SOURCE");
      return result;
    }
    if (!*requiresMaterialSourceVariant) {
      continue;
    }

    (void)table.registerShaderResource(pass.shaderUri, sourceUris.sourceUris,
                                       nullptr, true);

    for (auto &[type, typeFacts] : typeFactsByType) {
      if (!passAllowsType(pass, type)) {
        continue;
      }

      std::optional<LX_core::ShaderProgramSet> program =
          compileVariantShader(graphUri, pass, typeFacts,
                               sourceUris.sourceUris,
                               result.diagnostics);
      if (!program.has_value()) {
        return result;
      }

      const LX_core::StringID renderPathNodeSignature =
          LX_core::getRenderPathNodeSignature(pass);
      const LX_core::StringID materialTypeVariant =
          typeFacts.materials.front().material->getMaterialTypeVariantSignature(
              *program);
      table.registerMaterialSourceShaderVariant(
          pass.shaderUri, materialTypeVariant, renderPathNodeSignature,
          *program);
      attachVariantToMaterials(pass, *program, typeFacts);

      result.diagnostics.push_back(
          "MaterialSourceVariantResolver resolved type=" + type +
          " source=" + typeFacts.sourceUri.string() +
          " reflectionHash=" + typeFacts.reflectionHash +
          " sourceSignature=" + signatureDebug(typeFacts.sourceSignature) +
          " shader=" + pass.shaderUri.string() +
          " RenderPathNodeSignature=" +
          signatureDebug(renderPathNodeSignature) +
          " MaterialTypeVariant=" + signatureDebug(materialTypeVariant) +
          " bindingCount=" +
          std::to_string(
              program->getShader()
                  ? program->getShader()->getReflectionBindings().size()
                  : 0));
      ++result.resolvedVariantCount;
    }
  }

  result.success = result.resolvedVariantCount > 0 || result.diagnostics.empty();
  return result;
}

} // namespace LX_infra
