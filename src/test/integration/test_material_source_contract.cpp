#include "core/asset/material_contract.hpp"
#include "core/asset/material_contract_packer.hpp"
#include "core/asset/material_instance.hpp"
#include "core/frame_graph/pass.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_contract_reflector.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef LXE_SOURCE_DIR
#define LXE_SOURCE_DIR ""
#endif

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

void testSourceSignatureIgnoresInstanceValues() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri(
      "assets/shaders/glsl/common/materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "hash-a";
  a.storageAbiHash = "storage-a";
  a.accessorAbiHash = "accessor-v1";

  LX_core::MaterialContractReflection b = a;

  EXPECT(a.sourceSignature() == b.sourceSignature(),
         "same reflected source should produce same source signature");
  EXPECT(a.materialSignature(LX_core::StringID("ForwardPbr"),
                             LX_core::StringID("RenderStateOpaque")) ==
             b.materialSignature(LX_core::StringID("ForwardPbr"),
                                 LX_core::StringID("RenderStateOpaque")),
         "same reflected source should produce same material signature");

  b.reflectionHash = "hash-b";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "reflection hash must participate in source signature");
}

LX_core::MaterialContractReflection makePackerContract() {
  LX_core::MaterialContractReflection contract;
  contract.sourceUri =
      LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  contract.declaredType = "matte";
  contract.reflectionHash = "matte-reflect-v1";
  contract.storageAbiHash = "matte-storage-v1";
  contract.accessorAbiHash = "material-surface-v1";
  contract.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd",
      true,
      {LX_core::MaterialContractParameterKind::Rgb,
       LX_core::MaterialContractParameterKind::Texture}});
  contract.parameters.push_back(LX_core::MaterialContractParameter{
      "metallic",
      false,
      {LX_core::MaterialContractParameterKind::Float,
       LX_core::MaterialContractParameterKind::Texture}});
  contract.parameters.push_back(LX_core::MaterialContractParameter{
      "normalmap", false, {LX_core::MaterialContractParameterKind::Texture}});
  contract.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "baseColor",
      .type = LX_core::MaterialContractStorageFieldType::Vec4,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterValue,
      .parameterName = "Kd",
      .defaultValue = LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f},
  });
  contract.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "baseColorTexture",
      .type = LX_core::MaterialContractStorageFieldType::TextureSlot,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterTexture,
      .parameterName = "Kd",
      .defaultTextureSemantic = "white",
  });
  contract.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "baseColorChannel",
      .type = LX_core::MaterialContractStorageFieldType::ChannelSelector,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterChannel,
      .parameterName = "Kd",
      .defaultChannel = "rgba",
  });
  contract.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "metallic",
      .type = LX_core::MaterialContractStorageFieldType::Float,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterValue,
      .parameterName = "metallic",
      .defaultValue = LX_core::Vec4f{0.0f, 0.0f, 0.0f, 0.0f},
  });
  contract.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "normalTexture",
      .type = LX_core::MaterialContractStorageFieldType::TextureSlot,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterTexture,
      .parameterName = "normalmap",
      .defaultTextureSemantic = "flatNormal",
  });
  return contract;
}

LX_core::MaterialContractPackResult packWithDefaults(
    LX_core::MaterialContractDefaultTextureSlots defaults) {
  LX_core::MaterialContractPackInput input;
  input.contract = makePackerContract();
  input.defaultTextureSlots = defaults;
  input.sourceLocalMaterialIndex = 0;
  return LX_core::packMaterialContractRecord(input);
}

std::optional<
    std::reference_wrapper<const LX_core::SourceLocalMaterialFieldLayout>>
findPackedField(const LX_core::MaterialContractPackResult &result,
                std::string_view name) {
  return result.layout.findField(name);
}

u32 readPackedU32(const std::vector<u8> &bytes, usize offset) {
  u32 value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

float readPackedFloat(const std::vector<u8> &bytes, usize offset) {
  float value = 0.0f;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

void testPackerBuildsSourceReflectedBytesRecord() {
  LX_core::MaterialContractDefaultTextureSlots defaults;
  defaults.white = 1;
  defaults.black = 2;
  defaults.flatNormal = 3;

  auto materialTemplate = LX_core::MaterialTemplate::create("matte");
  auto material = LX_core::MaterialInstance::createUnique(materialTemplate);
  LX_core::MaterialParameterEnvelope kdTexture;
  kdTexture.kind = LX_core::MaterialEnvelopeKind::Texture;
  kdTexture.valueType = LX_core::MaterialEnvelopeValueType::Rgb;
  kdTexture.uri = "assets://textures/materials/base-color.png";
  material->setMaterialEnvelope(LX_core::StringID("Kd"), kdTexture);

  LX_core::MaterialParameterEnvelope metallicValue;
  metallicValue.kind = LX_core::MaterialEnvelopeKind::Float;
  metallicValue.floatValue = 0.75f;
  material->setMaterialEnvelope(LX_core::StringID("metallic"), metallicValue);

  LX_core::MaterialContractPackInput input;
  input.material = material.get();
  input.contract = makePackerContract();
  input.defaultTextureSlots = defaults;
  input.sourceLocalMaterialIndex = 7;
  input.textureSlotForUri = [](const LX_core::ResourceUri &uri) -> u32 {
    if (uri == LX_core::ResourceUri(
                   "assets://textures/materials/base-color.png")) {
      return 5;
    }
    return u32_max;
  };

  const auto packed = LX_core::packMaterialContractRecord(input);
  EXPECT(packed.diagnostics.empty(), "default-only material should pack");
  EXPECT(packed.record.sourceLocalMaterialIndex == 7,
         "packer should preserve source-local material index");
  EXPECT(!packed.record.bytes.empty(), "record bytes should be packed");

  const auto baseColorTexture = findPackedField(packed, "baseColorTexture");
  EXPECT(baseColorTexture.has_value(),
         "packed layout should expose baseColorTexture field");
  EXPECT(baseColorTexture.has_value() &&
             readPackedU32(packed.record.bytes,
                           baseColorTexture->get().offset) == 5,
         "base color texture slot should be packed from texture resolver");

  const auto metallic = findPackedField(packed, "metallic");
  EXPECT(metallic.has_value(), "packed layout should expose metallic field");
  EXPECT(metallic.has_value() &&
             readPackedFloat(packed.record.bytes, metallic->get().offset) ==
                 0.75f,
         "metallic factor should be packed from material envelope");

  const auto normalTexture = findPackedField(packed, "normalTexture");
  EXPECT(normalTexture.has_value(),
         "packed layout should expose normalTexture field");
  EXPECT(normalTexture.has_value() &&
             readPackedU32(packed.record.bytes,
                           normalTexture->get().offset) == 3,
         "missing normal texture should use flat normal default slot");
}

void testPackerRequiresEveryDefaultTextureSlot() {
  LX_core::MaterialContractDefaultTextureSlots defaults;
  defaults.white = 1;
  defaults.black = 2;
  defaults.flatNormal = 3;

  auto missingWhite = defaults;
  missingWhite.white = u32_max;
  EXPECT(!packWithDefaults(missingWhite).diagnostics.empty(),
         "missing white default slot should produce diagnostics");

  auto missingBlack = defaults;
  missingBlack.black = u32_max;
  EXPECT(!packWithDefaults(missingBlack).diagnostics.empty(),
         "missing black default slot should produce diagnostics");

  auto missingFlatNormal = defaults;
  missingFlatNormal.flatNormal = u32_max;
  EXPECT(!packWithDefaults(missingFlatNormal).diagnostics.empty(),
         "missing flatNormal default slot should produce diagnostics");
}

bool diagnosticsContain(const std::vector<std::string> &diagnostics,
                        const std::string &needle) {
  for (const std::string &diagnostic : diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool hasBsdfFunction(
    const LX_infra::MaterialContractReflectionResult &result,
    LX_infra::MaterialContractBsdfFunctionKind kind,
    std::string_view entryPoint) {
  for (const LX_infra::MaterialContractBsdfFunction &function :
       result.bsdfFunctions) {
    if (function.kind == kind && function.entryPoint == entryPoint) {
      return true;
    }
  }
  return false;
}

std::string_view
kindName(LX_core::MaterialContractParameterKind kind) {
  switch (kind) {
  case LX_core::MaterialContractParameterKind::Float:
    return "float";
  case LX_core::MaterialContractParameterKind::Rgb:
    return "rgb";
  case LX_core::MaterialContractParameterKind::Spectrum:
    return "spectrum";
  case LX_core::MaterialContractParameterKind::Texture:
    return "texture";
  case LX_core::MaterialContractParameterKind::Integer:
    return "integer";
  case LX_core::MaterialContractParameterKind::Bool:
    return "bool";
  case LX_core::MaterialContractParameterKind::String:
    return "string";
  case LX_core::MaterialContractParameterKind::MaterialRef:
    return "materialRef";
  case LX_core::MaterialContractParameterKind::BsdfTable:
    return "bsdfTable";
  }
  return "<unknown>";
}

std::string
formatKinds(std::initializer_list<LX_core::MaterialContractParameterKind> kinds) {
  std::ostringstream output;
  output << '[';
  bool first = true;
  for (const LX_core::MaterialContractParameterKind kind : kinds) {
    if (!first) {
      output << ", ";
    }
    output << kindName(kind);
    first = false;
  }
  output << ']';
  return output.str();
}

std::string formatKinds(
    const std::vector<LX_core::MaterialContractParameterKind> &kinds) {
  std::ostringstream output;
  output << '[';
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    if (i != 0) {
      output << ", ";
    }
    output << kindName(kinds[i]);
  }
  output << ']';
  return output.str();
}

std::optional<std::size_t> firstKindMismatchIndex(
    const std::vector<LX_core::MaterialContractParameterKind> &actual,
    std::initializer_list<LX_core::MaterialContractParameterKind> expected) {
  if (actual.size() != expected.size()) {
    return std::min(actual.size(), expected.size());
  }

  std::size_t index = 0;
  for (const LX_core::MaterialContractParameterKind kind : expected) {
    if (actual[index] != kind) {
      return index;
    }
    ++index;
  }
  return std::nullopt;
}

std::string kindNameAt(
    std::initializer_list<LX_core::MaterialContractParameterKind> kinds,
    std::size_t index) {
  std::size_t current = 0;
  for (const LX_core::MaterialContractParameterKind kind : kinds) {
    if (current == index) {
      return std::string(kindName(kind));
    }
    ++current;
  }
  return "<none>";
}

std::string kindNameAt(
    const std::vector<LX_core::MaterialContractParameterKind> &kinds,
    std::size_t index) {
  if (index >= kinds.size()) {
    return "<none>";
  }
  return std::string(kindName(kinds[index]));
}

std::string kindMismatchDetail(
    const std::vector<LX_core::MaterialContractParameterKind> &actual,
    std::initializer_list<LX_core::MaterialContractParameterKind> expected) {
  const std::size_t index =
      firstKindMismatchIndex(actual, expected).value_or(actual.size());
  return "index " + std::to_string(index) + " expected " +
         kindNameAt(expected, index) + ", actual " + kindNameAt(actual, index);
}

bool allowedKindsEqual(
    const LX_core::MaterialContractParameter &parameter,
    std::initializer_list<LX_core::MaterialContractParameterKind> kinds) {
  if (parameter.allowedKinds.size() != kinds.size()) {
    return false;
  }

  std::size_t index = 0;
  for (const LX_core::MaterialContractParameterKind kind : kinds) {
    if (parameter.allowedKinds[index] != kind) {
      return false;
    }
    ++index;
  }
  return true;
}

void expectContractParameter(
    const LX_core::MaterialContractReflection &reflection,
    std::string_view path,
    std::string_view name,
    bool required,
    std::initializer_list<LX_core::MaterialContractParameterKind> kinds) {
  const auto parameter = reflection.findParameter(name);
  EXPECT(parameter.has_value(),
         std::string(path) + " should declare parameter " + std::string(name));
  if (!parameter.has_value()) {
    return;
  }
  const std::string context = std::string(path) + " type " +
                              reflection.declaredType + " parameter " +
                              std::string(name);
  EXPECT(parameter->get().required == required,
         context + " required flag mismatch: expected " +
             (required ? "required" : "optional") + ", actual " +
             (parameter->get().required ? "required" : "optional"));
  EXPECT(allowedKindsEqual(parameter->get(), kinds),
         context + " allowed kind mismatch: " +
             kindMismatchDetail(parameter->get().allowedKinds, kinds) +
             "; expected " + formatKinds(kinds) + ", actual " +
             formatKinds(parameter->get().allowedKinds));
}

struct ContractParameterExpectation final {
  std::string_view name;
  bool required = false;
  std::initializer_list<LX_core::MaterialContractParameterKind> kinds;
};

void expectContractParameters(
    const LX_core::MaterialContractReflection &reflection,
    std::string_view path,
    std::initializer_list<ContractParameterExpectation> parameters) {
  EXPECT(reflection.parameters.size() == parameters.size(),
         std::string(path) + " should expose expected parameter count");
  for (const ContractParameterExpectation &parameter : parameters) {
    expectContractParameter(reflection, path, parameter.name,
                            parameter.required, parameter.kinds);
  }
}

bool storageFieldReferencesKnownInput(
    const LX_core::MaterialContractReflection &reflection,
    const LX_core::MaterialContractStorageField &field) {
  if (field.inputKind ==
      LX_core::MaterialContractStorageInputKind::Constant) {
    return true;
  }
  return reflection.findParameter(field.parameterName).has_value();
}

bool hasStorageField(const LX_core::MaterialContractReflection &reflection,
                     std::string_view name) {
  for (const LX_core::MaterialContractStorageField &field :
       reflection.storageFields) {
    if (field.name == name) {
      return true;
    }
  }
  return false;
}

void expectStorageFields(
    const LX_core::MaterialContractReflection &reflection,
    std::string_view path,
    std::initializer_list<std::string_view> requiredFields) {
  for (std::string_view field : requiredFields) {
    EXPECT(hasStorageField(reflection, field),
           std::string(path) + " should declare storage field " +
               std::string(field));
  }
}

void expectSupportedContractStorageFields(
    const LX_core::MaterialContractReflection &reflection,
    std::string_view path) {
  if (reflection.supportStatus !=
      LX_core::MaterialContractSupportStatus::Supported) {
    return;
  }

  EXPECT(!reflection.storageFields.empty(),
         std::string(path) +
             " supported contract should declare storage fields");
  for (const LX_core::MaterialContractStorageField &field :
       reflection.storageFields) {
    EXPECT(storageFieldReferencesKnownInput(reflection, field),
           std::string(path) + " storage field " + field.name +
               " should reference a declared parameter or constant");
  }
}

LX_infra::MaterialContractReflectionResult makeParserContract(
    const LX_core::ResourceUri &sourceUri, std::string_view sourceText) {
  EXPECT(!sourceText.empty(),
         "injected parser tests should receive explicit source text");

  LX_core::MaterialContractReflection reflection;
  reflection.sourceUri = sourceUri;
  reflection.declaredType = "matte";
  reflection.supportStatus =
      LX_core::MaterialContractSupportStatus::Supported;
  reflection.reflectionHash = "parser-test-reflection";
  reflection.storageAbiHash = "parser-test-storage";
  reflection.accessorAbiHash = "parser-test-accessor";
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd",
      true,
      {LX_core::MaterialContractParameterKind::Rgb,
       LX_core::MaterialContractParameterKind::Texture}});
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "sigma", true, {LX_core::MaterialContractParameterKind::Float}});

  LX_infra::MaterialContractReflectionResult result;
  result.reflection = std::move(reflection);
  return result;
}

LX_infra::MaterialContractSourceLoadResult makeParserContractSourceText(
    const LX_core::ResourceUri &sourceUri) {
  (void)sourceUri;
  LX_infra::MaterialContractSourceLoadResult result;
  result.sourceText = "injected material contract source";
  return result;
}

LX_core::MaterialTemplateSharedPtr makeSignedMaterialTemplate() {
  auto materialTemplate = LX_core::MaterialTemplate::create("matte");
  LX_core::MaterialPassDefinition passDefinition;
  passDefinition.shaderProgram.shaderName = "forward-pbr";
  materialTemplate->setPassDefinition(LX_core::Pass_Forward,
                                      std::move(passDefinition));
  return materialTemplate;
}

LX_infra::MaterialContractReflectionResult makeTypeMismatchParserContract(
    const LX_core::ResourceUri &sourceUri, std::string_view sourceText) {
  auto result = makeParserContract(sourceUri, sourceText);
  result.reflection->declaredType = "metal";
  return result;
}

LX_infra::MaterialContractReflectionResult makeUnsupportedParserContract(
    const LX_core::ResourceUri &sourceUri, std::string_view sourceText) {
  auto result = makeParserContract(sourceUri, sourceText);
  result.reflection->supportStatus =
      LX_core::MaterialContractSupportStatus::Unsupported;
  return result;
}

LX_infra::MaterialContractReflectionResult makeDiagnosticParserContract(
    const LX_core::ResourceUri &sourceUri, std::string_view sourceText) {
  (void)sourceText;
  LX_infra::MaterialContractReflectionResult result;
  result.diagnostics.push_back(sourceUri.string() +
                               ": synthetic reflector failure");
  return result;
}

void testMaterialParserRequiresBsdfSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/no-source.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr, "missing bsdf.source should fail");
  EXPECT(!parsed.diagnostics.empty(), "missing bsdf.source should diagnose");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "diagnostic should name bsdf.source");
}

void testMaterialParserRejectsUnknownParameterFromContract() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser(makeParserContract,
                                          makeParserContractSourceText);
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/bad-param.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
    legacyAlias: { kind: float, value: 1.0 }
)yaml");
  EXPECT(parsed.instance == nullptr,
         "contract unknown parameter should fail parse");
  EXPECT(diagnosticsContain(parsed.diagnostics,
                            "bsdf.parameters.legacyAlias"),
         "unknown parameter diagnostic should name parameter path");
}

void testMaterialParserRejectsSourceTypeMismatch() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser(makeTypeMismatchParserContract,
                                          makeParserContractSourceText);
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/type-mismatch.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/metal.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr,
         "contract declaredType mismatch should fail parse");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "type mismatch diagnostic should be attached to bsdf.source");
  EXPECT(diagnosticsContain(parsed.diagnostics, "metal"),
         "type mismatch diagnostic should name reflected type");
}

void testMaterialParserRejectsUnsupportedContract() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser(makeUnsupportedParserContract,
                                          makeParserContractSourceText);
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/unsupported.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr, "unsupported contract should fail parse");
  EXPECT(diagnosticsContain(parsed.diagnostics, "unsupported"),
         "unsupported contract diagnostic should be explicit");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "unsupported contract diagnostic should attach to bsdf.source");
}

void testMaterialParserRejectsBuiltInUnsupportedContracts() {
  struct UnsupportedCase final {
    const char *type;
    const char *source;
  };
  constexpr UnsupportedCase cases[] = {
      {"glass", "assets://shaders/glsl/common/materials/glass.contract.glsl"},
      {"fourier",
       "assets://shaders/glsl/common/materials/fourier.contract.glsl"},
      {"mix", "assets://shaders/glsl/common/materials/mix.contract.glsl"},
  };

  for (const UnsupportedCase &unsupported : cases) {
    LX_core::SceneResourceTable table;
    LX_infra::MaterialResourceParser parser;
    const auto parsed = parser.parse(
        table, LX_core::ResourceUri(std::string("memory://materials/") +
                                    unsupported.type + ".material"),
        std::string(R"yaml(
schema: lxe.material.v2
bsdf:
  type: )yaml") +
            unsupported.type + R"yaml(
  source: )yaml" +
            unsupported.source + R"yaml(
  parameters: {}
)yaml");

    EXPECT(parsed.instance == nullptr,
           std::string(unsupported.type) +
               " built-in unsupported contract should fail parse");
    EXPECT(diagnosticsContain(parsed.diagnostics, "unsupported"),
           std::string(unsupported.type) +
               " built-in unsupported contract should diagnose unsupported");
    EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
           std::string(unsupported.type) +
               " built-in unsupported diagnostic should name bsdf.source");
  }
}

void testMaterialParserAttachesReflectDiagnosticsToSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser(makeDiagnosticParserContract,
                                          makeParserContractSourceText);
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/reflect-fails.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/broken.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr,
         "reflect diagnostics should fail material parse");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "reflect diagnostic should be attached to bsdf.source");
  EXPECT(diagnosticsContain(parsed.diagnostics, "synthetic reflector failure"),
         "reflect diagnostic should preserve reflector message");
}

void testMaterialParserRejectsKindNotAllowedByContract() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser(makeParserContract,
                                          makeParserContractSourceText);
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/bad-kind.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/matte.contract.glsl
  parameters:
    Kd: { kind: string, value: bad }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr,
         "contract disallowed parameter kind should fail parse");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.parameters.Kd"),
         "disallowed kind diagnostic should name parameter path");
  EXPECT(diagnosticsContain(parsed.diagnostics, "not allowed"),
         "disallowed kind diagnostic should be explicit");
}

void testMaterialParserStoresReflectedSourceIdentity() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser(makeParserContract,
                                          makeParserContractSourceText);
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/source-identity.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.diagnostics.empty(),
         "valid reflected contract material should parse");
  EXPECT(parsed.instance != nullptr,
         "valid reflected contract material should create an instance");
  if (!parsed.instance) {
    return;
  }

  const LX_core::ResourceUri expectedSourceUri(
      "memory://materials/matte.contract.glsl");
  const auto expectedSourceText = makeParserContractSourceText(
      expectedSourceUri);
  auto expectedReflection =
      makeParserContract(expectedSourceUri, *expectedSourceText.sourceText);
  EXPECT(parsed.instance->getMaterialSourceUri().string() ==
             "memory://materials/matte.contract.glsl",
         "MaterialInstance should store reflected source URI");
  EXPECT(parsed.instance->getMaterialSourceReflectionHash() ==
             "parser-test-reflection",
         "MaterialInstance should store reflected source hash");
  EXPECT(parsed.instance->getMaterialSourceSignature() ==
             expectedReflection.reflection->sourceSignature(),
         "MaterialInstance should store reflected source signature");
}

void testDefaultMaterialParserRejectsMemoryContractSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/default-memory.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: memory://materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr,
         "default parser should not treat memory contract sources as empty "
         "files");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "memory source rejection should attach to bsdf.source");
  EXPECT(diagnosticsContain(parsed.diagnostics, "memory://"),
         "memory source rejection should name the unsupported URI");
  EXPECT(diagnosticsContain(parsed.diagnostics, "unsupported"),
         "memory source rejection should be explicit");
}

void testSharedContractLoaderReadsBuiltInAssetSource() {
  const auto loaded = LX_infra::loadMaterialContractSourceText(
      LX_core::ResourceUri(
          "assets://shaders/glsl/common/materials/matte.contract.glsl"));
  EXPECT(loaded.diagnostics.empty(),
         "shared contract source loader should read assets URI");
  EXPECT(loaded.sourceText.has_value(),
         "shared contract source loader should return asset text");
  if (!loaded.sourceText.has_value()) {
    return;
  }
  const auto reflected = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "assets://shaders/glsl/common/materials/matte.contract.glsl"),
      *loaded.sourceText);
  EXPECT(reflected.diagnostics.empty(),
         "built-in matte contract should reflect from shared loader text");
  EXPECT(reflected.reflection.has_value(),
         "built-in matte contract should produce reflection");
}

void testBuiltInContractSourcesReflectActiveSchemas() {
  using Kind = LX_core::MaterialContractParameterKind;
  struct BuiltInContractCase final {
    const char *path;
    const char *declaredType;
    const char *storageAbiHash;
    LX_core::MaterialContractSupportStatus supportStatus;
  };
  constexpr BuiltInContractCase cases[] = {
      {"assets://shaders/glsl/common/materials/matte.contract.glsl", "matte",
       "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Supported},
      {"assets://shaders/glsl/common/materials/glass.contract.glsl", "glass",
       "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Unsupported},
      {"assets://shaders/glsl/common/materials/uber.contract.glsl", "uber",
       "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Supported},
      {"assets://shaders/glsl/common/materials/metal.contract.glsl", "metal",
       "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Supported},
      {"assets://shaders/glsl/common/materials/substrate.contract.glsl",
       "substrate", "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Supported},
      {"assets://shaders/glsl/common/materials/standard_pbr.contract.glsl",
       "standard-pbr", "standard-pbr-storage-v1",
       LX_core::MaterialContractSupportStatus::Supported},
      {"assets://shaders/glsl/common/materials/fourier.contract.glsl",
       "fourier", "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Unsupported},
      {"assets://shaders/glsl/common/materials/mix.contract.glsl", "mix",
       "pbrt-envelope-storage-v1",
       LX_core::MaterialContractSupportStatus::Unsupported}};

  for (const BuiltInContractCase &contractCase : cases) {
    const std::string path(contractCase.path);
    const auto reflected = LX_infra::loadAndReflectMaterialContractSource(
        LX_core::ResourceUri(path));
    EXPECT(reflected.diagnostics.empty(),
           path + " should load and reflect without diagnostics");
    EXPECT(reflected.reflection.has_value(),
           path + " should produce reflected contract metadata");
    if (!reflected.reflection.has_value()) {
      continue;
    }

    const LX_core::MaterialContractReflection &reflection =
        *reflected.reflection;
    EXPECT(reflection.declaredType == contractCase.declaredType,
           path + " should declare expected material type");
    EXPECT(reflection.supportStatus == contractCase.supportStatus,
           path + " should declare expected support status");
    EXPECT(reflection.reflectionHash ==
               std::string(contractCase.declaredType) + "-source-contract-v1",
           path + " should declare expected reflection hash");
    EXPECT(reflection.storageAbiHash == contractCase.storageAbiHash,
           path + " should declare expected storage ABI hash");
    EXPECT(reflection.accessorAbiHash == "material-surface-v1",
           path + " should declare expected accessor ABI hash");
    EXPECT(reflection.accessorAbi.entryPoint == "lxLoadMaterialSurface",
           path + " should declare Material Accessor ABI entry point");
    if (reflection.supportStatus ==
        LX_core::MaterialContractSupportStatus::Supported) {
      EXPECT(hasBsdfFunction(
                 reflected,
                 LX_infra::MaterialContractBsdfFunctionKind::Evaluate,
                 "lxEvaluateBsdf"),
             path + " supported contract should declare evaluate BSDF "
                    "function metadata");
      EXPECT(hasBsdfFunction(
                 reflected,
                 LX_infra::MaterialContractBsdfFunctionKind::Sample,
                 "lxSampleBsdf"),
             path + " supported contract should declare sample BSDF function "
                    "metadata");
    }

    if (reflection.declaredType == "matte") {
      expectContractParameters(
          reflection, path,
          {{"Kd", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"sigma", true, {Kind::Float, Kind::Texture}},
           {"normalmap", false, {Kind::Texture}}});
    } else if (reflection.declaredType == "glass") {
      expectContractParameters(
          reflection, path,
          {{"Kr", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"Kt", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"eta", true, {Kind::Float, Kind::Texture}},
           {"uroughness", true, {Kind::Float, Kind::Texture}},
           {"vroughness", true, {Kind::Float, Kind::Texture}},
           {"normalmap", false, {Kind::Texture}}});
    } else if (reflection.declaredType == "uber") {
      expectContractParameters(
          reflection, path,
          {{"Kd", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"Ks", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"Kr", false, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"Kt", false, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"opacity", false, {Kind::Rgb, Kind::Texture}},
           {"eta", false, {Kind::Float, Kind::Texture}},
           {"uroughness", false, {Kind::Float, Kind::Texture}},
           {"vroughness", false, {Kind::Float, Kind::Texture}},
           {"normalmap", false, {Kind::Texture}}});
    } else if (reflection.declaredType == "metal") {
      expectContractParameters(
          reflection, path,
          {{"eta", true, {Kind::Spectrum}},
           {"k", true, {Kind::Spectrum}},
           {"uroughness", false, {Kind::Float, Kind::Texture}},
           {"vroughness", false, {Kind::Float, Kind::Texture}},
           {"normalmap", false, {Kind::Texture}}});
    } else if (reflection.declaredType == "substrate") {
      expectContractParameters(
          reflection, path,
          {{"Kd", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"Ks", true, {Kind::Rgb, Kind::Texture, Kind::Spectrum}},
           {"uroughness", true, {Kind::Float, Kind::Texture}},
           {"vroughness", true, {Kind::Float, Kind::Texture}},
           {"normalmap", false, {Kind::Texture}}});
    } else if (reflection.declaredType == "standard-pbr") {
      expectContractParameters(
          reflection, path,
          {{"baseColor", false, {Kind::Rgb}},
           {"baseColorTexture", false, {Kind::Texture}},
           {"metallic", false, {Kind::Float}},
           {"metallicRoughnessTexture", false, {Kind::Texture}},
           {"roughness", false, {Kind::Float}},
           {"normalTexture", false, {Kind::Texture}},
           {"occlusionTexture", false, {Kind::Texture}},
           {"emissive", false, {Kind::Rgb}},
           {"emissiveTexture", false, {Kind::Texture}},
           {"alphaMode", false, {Kind::String}},
           {"alphaCutoff", false, {Kind::Float}}});
      expectStorageFields(reflection, path,
                          {"baseColor", "baseColorTexture", "metallic",
                           "metallicRoughnessTexture", "roughness",
                           "normalTexture", "occlusionTexture", "emissive",
                           "emissiveTexture", "alphaMode", "alphaCutoff"});
    } else if (reflection.declaredType == "fourier") {
      expectContractParameters(
          reflection, path,
          {{"bsdffile", true, {Kind::BsdfTable}},
           {"normalmap", false, {Kind::Texture}}});
    } else if (reflection.declaredType == "mix") {
      expectContractParameters(
          reflection, path,
          {{"namedmaterial1", true, {Kind::MaterialRef}},
           {"namedmaterial2", true, {Kind::MaterialRef}},
           {"amount", true, {Kind::Float}},
           {"normalmap", false, {Kind::Texture}}});
    }
    expectSupportedContractStorageFields(reflection, path);

    const auto loaded =
        LX_infra::loadMaterialContractSourceText(LX_core::ResourceUri(path));
    EXPECT(loaded.sourceText.has_value(),
           path + " should be readable for default assignment checks");
    if (!loaded.sourceText.has_value()) {
      continue;
    }
    const std::string &source = *loaded.sourceText;
    EXPECT(source.find("surface.baseColor") != std::string::npos,
           path + " should initialize baseColor");
    EXPECT(source.find("surface.alpha") != std::string::npos,
           path + " should initialize alpha");
    EXPECT(source.find("surface.metallic") != std::string::npos,
           path + " should initialize metallic");
    EXPECT(source.find("surface.roughness") != std::string::npos,
           path + " should initialize roughness");
    EXPECT(source.find("surface.normal") != std::string::npos,
           path + " should initialize normal");
    EXPECT(source.find("surface.ao") != std::string::npos,
           path + " should initialize ao");
    EXPECT(source.find("surface.emissive") != std::string::npos,
           path + " should initialize emissive");
  }
}

void testMaterialParserRejectsNonScalarBsdfSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(table,
                                   LX_core::ResourceUri("memory://materials/"
                                                        "non-scalar-source.material"),
                                   R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: [materials/matte.contract.glsl]
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr, "non-scalar bsdf.source should fail");
  EXPECT(!parsed.diagnostics.empty(),
         "non-scalar bsdf.source should diagnose");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "non-scalar diagnostic should name bsdf.source");
}

void testMaterialParserRejectsEmptyBsdfSource() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/empty-source.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: ""
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.instance == nullptr, "empty bsdf.source should fail");
  EXPECT(!parsed.diagnostics.empty(), "empty bsdf.source should diagnose");
  EXPECT(diagnosticsContain(parsed.diagnostics, "bsdf.source"),
         "empty source diagnostic should name bsdf.source");
}

void testMaterialParserStoresSourceIdentityAndCloneCopiesIt() {
  LX_core::SceneResourceTable table;
  LX_infra::MaterialResourceParser parser;
  const auto parsed = parser.parse(
      table, LX_core::ResourceUri("memory://materials/with-source.material"),
      R"yaml(
schema: lxe.material.v2
bsdf:
  type: matte
  source: assets://shaders/glsl/common/materials/matte.contract.glsl
  parameters:
    Kd: { kind: rgb, value: [1.0, 1.0, 1.0] }
    sigma: { kind: float, value: 0.0 }
)yaml");
  EXPECT(parsed.diagnostics.empty(), "explicit bsdf.source should parse");
  EXPECT(parsed.instance != nullptr,
         "explicit bsdf.source should create instance");
  if (!parsed.instance) {
    return;
  }

  EXPECT(parsed.instance->getMaterialSourceUri().string() ==
             "assets://shaders/glsl/common/materials/matte.contract.glsl",
         "MaterialInstance should store canonical source URI");
  EXPECT(parsed.instance->getMaterialSourceSignature() != LX_core::StringID{},
         "MaterialInstance should store reflected source signature");
  EXPECT(parsed.instance->getMaterialSourceReflectionHash() ==
             "matte-source-contract-v1",
         "MaterialInstance should store reflected source hash");
  const auto contract = parsed.instance->getMaterialContractReflection();
  EXPECT(contract.has_value(),
         "MaterialInstance should store reflected contract data");
  EXPECT(contract.has_value() && contract->get().declaredType == "matte",
         "stored reflected contract should retain declared type");
  const auto kd = contract.has_value() ? contract->get().findParameter("Kd")
                                       : std::nullopt;
  EXPECT(kd.has_value(), "stored reflected contract should expose Kd");
  EXPECT(kd.has_value() && kd->get().allowedKinds.size() == 3,
         "stored reflected contract should retain Kd allowed kinds");

  const auto clone = parsed.instance->cloneInstanceDataUnique();
  EXPECT(clone->getMaterialSourceUri() ==
             parsed.instance->getMaterialSourceUri(),
         "clone should copy material source URI");
  EXPECT(clone->getMaterialSourceSignature() ==
             parsed.instance->getMaterialSourceSignature(),
         "clone should copy material source signature");
  EXPECT(clone->getMaterialSourceReflectionHash() ==
             parsed.instance->getMaterialSourceReflectionHash(),
         "clone should copy material source reflection hash");
  const auto cloneContract = clone->getMaterialContractReflection();
  EXPECT(cloneContract.has_value(),
         "clone should copy reflected contract data");
  EXPECT(cloneContract.has_value() &&
             cloneContract->get().sourceSignature() ==
                 contract->get().sourceSignature(),
         "clone should copy reflected contract source signature");
  const auto cloneKd = cloneContract.has_value()
                           ? cloneContract->get().findParameter("Kd")
                           : std::nullopt;
  EXPECT(cloneKd.has_value(),
         "clone reflected contract should expose Kd parameter");
}

void testMaterialInstancePipelineSignatureUsesSourceSignature() {
  auto material =
      LX_core::MaterialInstance::createUnique(makeSignedMaterialTemplate());
  material->setMaterialSourceUri(LX_core::ResourceUri(
      "assets://shaders/glsl/common/materials/matte.contract.glsl"));
  material->setMaterialSourceReflectionHash("matte-source-contract-v1");
  material->setMaterialSourceSignature(LX_core::StringID("source-a"));

  const LX_core::StringID forward =
      material->getPipelineSignature(LX_core::Pass_Forward);
  material->setMaterialEnvelope(LX_core::StringID("Kd"),
                                LX_core::MaterialParameterEnvelope{});
  const LX_core::StringID afterValueChange =
      material->getPipelineSignature(LX_core::Pass_Forward);

  EXPECT(forward == afterValueChange,
         "material parameter values must not alter material pipeline "
         "signature");

  LX_core::MaterialResourceDependency textureDependency;
  textureDependency.kind = LX_core::MaterialEnvelopeKind::Texture;
  textureDependency.uri =
      LX_core::ResourceUri("assets://textures/materials/matte-kd.png");
  textureDependency.parameterName = "Kd";
  material->addMaterialDependency(std::move(textureDependency));
  const LX_core::StringID afterDependencyChange =
      material->getPipelineSignature(LX_core::Pass_Forward);

  EXPECT(forward == afterDependencyChange,
         "material dependency mutations must not alter material pipeline "
         "signature");

  auto other =
      LX_core::MaterialInstance::createUnique(material->getTemplate());
  other->setMaterialSourceUri(LX_core::ResourceUri(
      "assets://shaders/glsl/common/materials/matte.contract.glsl"));
  other->setMaterialSourceReflectionHash("matte-source-contract-v2");
  other->setMaterialSourceSignature(LX_core::StringID("source-b"));

  EXPECT(forward != other->getPipelineSignature(LX_core::Pass_Forward),
         "different reflected source signatures must produce different "
         "material pipeline signatures");

  const std::string debug =
      LX_core::GlobalStringTable::get().toDebugString(forward);
  EXPECT(debug.find("MaterialRender(") != std::string::npos,
         "source-reflected material signature debug string should include "
         "MaterialRender(, got: " +
             debug);
  EXPECT(debug.find("source-a") != std::string::npos,
         "source-reflected material signature debug string should include "
         "source signature, got: " +
             debug);
}

void testLegacyMaterialPipelineSignatureIgnoresInstanceValues() {
  auto material =
      LX_core::MaterialInstance::createUnique(makeSignedMaterialTemplate());
  const LX_core::StringID before =
      material->getPipelineSignature(LX_core::Pass_Forward);
  material->setMaterialEnvelope(LX_core::StringID("Kd"),
                                LX_core::MaterialParameterEnvelope{});
  const LX_core::StringID after =
      material->getPipelineSignature(LX_core::Pass_Forward);

  auto sameTemplate =
      LX_core::MaterialInstance::createUnique(material->getTemplate());
  EXPECT(before == after,
         "legacy material pipeline signature must ignore instance values");
  EXPECT(before == sameTemplate->getPipelineSignature(LX_core::Pass_Forward),
         "legacy material pipeline signature should be determined by "
         "template/pass signature");
}

void testFindParameterHitAndMiss() {
  LX_core::MaterialContractReflection reflection;
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});
  reflection.parameters.push_back(LX_core::MaterialContractParameter{
      "roughness", false, {LX_core::MaterialContractParameterKind::Float}});

  auto hit = reflection.findParameter("roughness");
  EXPECT(hit.has_value(), "findParameter should find an existing parameter");
  EXPECT(hit.has_value() && hit->get().name == "roughness",
         "findParameter should return the matching parameter");
  EXPECT(!reflection.findParameter("metallic").has_value(),
         "findParameter should return empty for a missing parameter");
}

void testDefaultAccessorAbi() {
  LX_core::MaterialContractAccessorAbi abi;
  constexpr std::array expectedFields{"baseColor", "alpha",  "metallic",
                                      "roughness", "normal", "ao",
                                      "emissive"};

  EXPECT(abi.entryPoint == "lxLoadMaterialSurface",
         "default accessor entry point should match Material Accessor ABI");
  EXPECT(abi.requiredFields.size() == expectedFields.size(),
         "default accessor required field count should match ABI");
  for (std::size_t i = 0;
       i < abi.requiredFields.size() && i < expectedFields.size(); ++i) {
    EXPECT(abi.requiredFields[i] == expectedFields[i],
           "default accessor required fields should match ABI order");
  }
}

void testSourceSignatureIncludesStorageAndAccessorAbi() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri(
      "assets/shaders/glsl/common/materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "hash-a";
  a.storageAbiHash = "storage-a";
  a.accessorAbiHash = "accessor-v1";

  LX_core::MaterialContractReflection b = a;
  b.storageAbiHash = "storage-b";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "storage ABI hash must participate in source signature");

  b = a;
  b.accessorAbiHash = "accessor-v2";
  EXPECT(a.sourceSignature() != b.sourceSignature(),
         "accessor ABI hash must participate in source signature");
}

void testMaterialSignatureIncludesPassAndRenderState() {
  LX_core::MaterialContractReflection reflection;
  reflection.sourceUri = LX_core::ResourceUri(
      "assets/shaders/glsl/common/materials/matte.contract.glsl");
  reflection.declaredType = "matte";
  reflection.reflectionHash = "hash-a";
  reflection.storageAbiHash = "storage-a";
  reflection.accessorAbiHash = "accessor-v1";

  const LX_core::StringID forwardPbr("ForwardPbr");
  const LX_core::StringID deferredPbr("DeferredPbr");
  const LX_core::StringID opaque("RenderStateOpaque");
  const LX_core::StringID alphaBlend("RenderStateAlphaBlend");

  EXPECT(reflection.materialSignature(forwardPbr, opaque) !=
             reflection.materialSignature(deferredPbr, opaque),
         "pass shader signature must participate in material signature");
  EXPECT(reflection.materialSignature(forwardPbr, opaque) !=
             reflection.materialSignature(forwardPbr, alphaBlend),
         "render state signature must participate in material signature");
}

void testReflectsContractMetadataBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// parameter: metallic optional float texture
// parameter: roughness optional float texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/matte.contract.glsl"), source);

  EXPECT(result.diagnostics.empty(),
         "valid contract source should not emit diagnostics");
  EXPECT(result.reflection.has_value(),
         "valid contract source should reflect a contract");
  EXPECT(result.reflection.has_value() &&
             result.reflection->declaredType == "matte",
         "contract should reflect declared type");
  EXPECT(result.reflection.has_value() &&
             result.reflection->supportStatus ==
                 LX_core::MaterialContractSupportStatus::Supported,
         "contract should reflect support status");
  EXPECT(result.reflection.has_value() &&
             result.reflection->reflectionHash == "matte-reflect-v1",
         "contract should reflect reflection hash");
  EXPECT(result.reflection.has_value() &&
             result.reflection->storageAbiHash == "matte-storage-v1",
         "contract should reflect storage ABI hash");
  EXPECT(result.reflection.has_value() &&
             result.reflection->accessorAbiHash == "material-surface-v1",
         "contract should reflect accessor ABI hash");

  const auto kd = result.reflection.has_value()
                      ? result.reflection->findParameter("Kd")
                      : std::nullopt;
  EXPECT(kd.has_value(), "contract should reflect Kd parameter");
  EXPECT(kd.has_value() && kd->get().required,
         "contract should reflect Kd required flag");
  EXPECT(kd.has_value() && kd->get().allowedKinds.size() == 3,
         "contract should reflect Kd kind count");
  EXPECT(kd.has_value() && kd->get().allowedKinds.size() == 3 &&
             kd->get().allowedKinds[0] ==
                 LX_core::MaterialContractParameterKind::Rgb &&
             kd->get().allowedKinds[1] ==
                 LX_core::MaterialContractParameterKind::Texture &&
             kd->get().allowedKinds[2] ==
                 LX_core::MaterialContractParameterKind::Spectrum,
         "contract should reflect Kd allowed kind order");

  const auto metallic = result.reflection.has_value()
                            ? result.reflection->findParameter("metallic")
                            : std::nullopt;
  EXPECT(metallic.has_value(), "contract should reflect metallic parameter");
  EXPECT(metallic.has_value() && !metallic->get().required,
         "contract should reflect metallic optional flag");
  EXPECT(metallic.has_value() && metallic->get().allowedKinds.size() == 2,
         "contract should reflect metallic kind count");
  EXPECT(metallic.has_value() && metallic->get().allowedKinds.size() == 2 &&
             metallic->get().allowedKinds[0] ==
                 LX_core::MaterialContractParameterKind::Float &&
             metallic->get().allowedKinds[1] ==
                 LX_core::MaterialContractParameterKind::Texture,
         "contract should reflect metallic allowed kind order");
}

void testReflectsBsdfFunctionMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  return surface;
}
LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput) {
  LxBsdfEvaluateOutput output;
  output.value = bsdfInput.baseColor;
  return output;
}
LxBsdfSampleOutput lxSampleBsdf(LxBsdfSampleInput bsdfInput) {
  LxBsdfSampleOutput output;
  output.wi = bsdfInput.normal;
  output.value = vec3(1.0);
  output.pdf = 1.0;
  return output;
}
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/bsdf.contract.glsl"), source);

  EXPECT(result.diagnostics.empty(),
         "valid BSDF function metadata should not emit diagnostics");
  EXPECT(result.reflection.has_value(),
         "valid BSDF function metadata should reflect a contract");
  EXPECT(hasBsdfFunction(result,
                         LX_infra::MaterialContractBsdfFunctionKind::Evaluate,
                         "lxEvaluateBsdf"),
         "contract should record evaluate BSDF function");
  EXPECT(hasBsdfFunction(result,
                         LX_infra::MaterialContractBsdfFunctionKind::Sample,
                         "lxSampleBsdf"),
         "contract should record sample BSDF function");
}

void testReflectRejectsBsdfFunctionWithoutAbiDefinition() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// bsdfFunction: evaluate lxEvaluateBsdf
// bsdfFunction: sample lxSampleBsdf
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  return surface;
}
LxBsdfEvaluateOutput lxEvaluateBsdf(LxBsdfEvaluateInput bsdfInput);
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/missing-bsdf.contract.glsl"),
      source);

  EXPECT(diagnosticsContain(result.diagnostics, "lxSampleBsdf"),
         "missing sample BSDF ABI definition should be diagnostic");
  EXPECT(!result.reflection.has_value(),
         "missing BSDF ABI definition should reject reflection");
}

void testReflectsMaterialStorageFields() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// storageField: baseColor vec4 parameter Kd value default=1,1,1,1
// storageField: baseColorTexture textureSlot parameter Kd texture defaultTexture=white
// storageField: baseColorChannel channelSelector parameter Kd channel default=rgba
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  return surface;
}
)glsl";

  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/storage-fields.contract.glsl"),
      source);

  EXPECT(result.diagnostics.empty(),
         "storage field reflection should not emit diagnostics");
  EXPECT(result.reflection.has_value(),
         "storage field reflection should produce metadata");
  if (!result.reflection.has_value()) {
    return;
  }

  const auto &fields = result.reflection->storageFields;
  EXPECT(fields.size() == 3, "three storage fields should be reflected");
  EXPECT(fields[0].name == "baseColor",
         "first storage field should preserve name");
  EXPECT(fields[0].type == LX_core::MaterialContractStorageFieldType::Vec4,
         "baseColor should be reflected as vec4");
  EXPECT(fields[0].inputKind ==
             LX_core::MaterialContractStorageInputKind::ParameterValue,
         "baseColor should read parameter value");
  EXPECT(fields[0].parameterName == "Kd",
         "baseColor should reference Kd parameter");
  EXPECT(fields[0].defaultValue.x == 1.0f &&
             fields[0].defaultValue.y == 1.0f &&
             fields[0].defaultValue.z == 1.0f &&
             fields[0].defaultValue.w == 1.0f,
         "baseColor should reflect default value");

  EXPECT(fields[1].name == "baseColorTexture",
         "second storage field should preserve texture field name");
  EXPECT(fields[1].type ==
             LX_core::MaterialContractStorageFieldType::TextureSlot,
         "baseColorTexture should be reflected as textureSlot");
  EXPECT(fields[1].inputKind ==
             LX_core::MaterialContractStorageInputKind::ParameterTexture,
         "baseColorTexture should read parameter texture");
  EXPECT(fields[1].defaultTextureSemantic == "white",
         "baseColorTexture should reflect white default texture");

  EXPECT(fields[2].type ==
             LX_core::MaterialContractStorageFieldType::ChannelSelector,
         "baseColorChannel should be reflected as channelSelector");
  EXPECT(fields[2].inputKind ==
             LX_core::MaterialContractStorageInputKind::ParameterChannel,
         "baseColorChannel should read parameter channel");
  EXPECT(fields[2].defaultChannel == "rgba",
         "baseColorChannel should reflect default channel");
}

void testReflectRejectsDuplicateStorageFieldMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// storageField: baseColor vec4 parameter Kd value default=1,1,1,1
// storageField: baseColor textureSlot parameter Kd texture defaultTexture=white
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  return surface;
}
)glsl";

  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-storage-field.contract.glsl"),
      source);

  EXPECT(diagnosticsContain(result.diagnostics, "duplicate storage field"),
         "duplicate storage field names should be diagnostic");
  EXPECT(!result.reflection.has_value(),
         "duplicate storage fields should reject reflection");
}

void testReflectRejectsUnknownStorageFieldType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-source-contract-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture spectrum
// storageField: baseColor matrix4 parameter Kd value default=1,1,1,1
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  LxMaterialSurface surface;
  return surface;
}
)glsl";

  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/unknown-storage-field-type.contract.glsl"),
      source);

  EXPECT(diagnosticsContain(result.diagnostics, "unknown storage field type"),
         "unknown storage field types should be diagnostic");
  EXPECT(!result.reflection.has_value(),
         "unknown storage field types should reject reflection");
}

void testReflectsAccessorWithBodyComment() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) {
  /* body comment */
  LxMaterialSurface surface;
  return surface;
}
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/body-comment.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "function body comments should not affect accessor detection");
  EXPECT(result.reflection.has_value(),
         "function body comments should still reflect a contract");
}

void testReflectRejectsMissingAccessor() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/bad.contract.glsl"), source);
  EXPECT(!result.diagnostics.empty(),
         "missing Material Accessor ABI should be diagnostic");
  EXPECT(!result.reflection.has_value(),
         "missing accessor should reject reflection");
}

void testReflectRejectsMissingStatus() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/missing-status.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "missing status should emit a diagnostic");
  EXPECT(!result.reflection.has_value(),
         "missing status should reject reflection");
}

void testReflectRejectsDuplicateTypeMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// type: glossy
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/duplicate-type.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(), "duplicate type should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate type should reject reflection");
}

void testReflectRejectsDuplicateStatusMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// status: unsupported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/duplicate-status.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate status should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate status should reject reflection");
}

void testReflectRejectsDuplicateReflectionHashMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// reflectionHash: matte-reflect-v2
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-reflection-hash.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate reflection hash should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate reflection hash should reject reflection");
}

void testReflectRejectsDuplicateStorageAbiHashMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// storageAbiHash: matte-storage-v2
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-storage-hash.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate storage ABI hash should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate storage ABI hash should reject reflection");
}

void testReflectRejectsDuplicateAccessorAbiHashMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// accessorAbiHash: material-surface-v2
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-accessor-hash.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate accessor ABI hash should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate accessor ABI hash should reject reflection");
}

void testReflectRejectsDuplicateParameterMetadata() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// parameter: Kd optional float
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/duplicate-parameter.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "duplicate parameter should emit diagnostics");
  EXPECT(!result.reflection.has_value(),
         "duplicate parameter should reject reflection");
}

void testReflectRejectsBareMetadataLines() {
  const std::string source = R"glsl(
LX_MATERIAL_CONTRACT_BEGIN
type: matte
status: supported
reflectionHash: matte-reflect-v1
storageAbiHash: matte-storage-v1
accessorAbiHash: material-surface-v1
parameter: Kd required rgb texture
LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/bare.contract.glsl"), source);
  EXPECT(!result.diagnostics.empty(),
         "bare metadata lines should not be accepted as contract metadata");
  EXPECT(!result.reflection.has_value(),
         "bare metadata lines should reject reflection");
}

void testReflectRejectsCommentedOutAccessor() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
// LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/commented-accessor.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "commented-out accessor should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "commented-out accessor should reject reflection");
}

void testReflectRejectsAccessorCallWithoutDefinition() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
void probe() { lxLoadMaterialSurface(0, uv, n, tbn); }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-call.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor call should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor call should reject reflection");
}

void testReflectRejectsAccessorPrototypeWithoutBody() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame);
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-prototype.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor prototype should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor prototype should reject reflection");
}

void testReflectRejectsWrongAccessorReturnType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
float lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-return.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor return type should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor return type should reject reflection");
}

void testReflectRejectsQualifiedAccessorReturnType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
const LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-qualified-return.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "qualified accessor return type should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "qualified accessor return type should reject reflection");
}

void testReflectRejectsPrefixedAccessorReturnType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
inline LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-prefixed-return.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "prefixed accessor return type should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "prefixed accessor return type should reject reflection");
}

void testReflectRejectsCommentBetweenAccessorReturnAndName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface /* comment */ lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-comment-before-name.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "comment between accessor return type and name should not satisfy "
         "Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "comment between accessor return type and name should reject "
         "reflection");
}

void testReflectRejectsWrongAccessorFunctionName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadNotMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-name.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor function name should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor function name should reject reflection");
}

void testReflectRejectsWrongAccessorParameterList() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-params.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor parameter list should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor parameter list should reject reflection");
}

void testReflectRejectsExtraAccessorParameter() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame, float extra) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-extra-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "extra accessor parameter should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "extra accessor parameter should reject reflection");
}

void testReflectRejectsWrongAccessorParameterType() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(int materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-param-type.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor parameter type should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor parameter type should reject reflection");
}

void testReflectRejectsAccessorArrayDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex[2], vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-array-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor array declarator should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor array declarator should reject reflection");
}

void testReflectRejectsAccessorNumericParameterName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint 2bad, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-numeric-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor numeric parameter name should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor numeric parameter name should reject reflection");
}

void testReflectRejectsCommentInsideAccessorParameter() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex /* comment */, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-comment-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "comment inside accessor parameter should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "comment inside accessor parameter should reject reflection");
}

void testReflectRejectsCommentBetweenAccessorParametersAndBody() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) /* comment */ { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-comment-before-body.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "comment between accessor parameter list and body should not satisfy "
         "Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "comment between accessor parameter list and body should reject "
         "reflection");
}

void testReflectRejectsAccessorPointerDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint *materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-pointer-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor pointer declarator should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor pointer declarator should reject reflection");
}

void testReflectRejectsAccessorReferenceDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint &materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-reference-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor reference declarator should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor reference declarator should reject reflection");
}

void testReflectRejectsAccessorQualifierDeclarator() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(const uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-qualifier-param.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor qualifier declarator should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor qualifier declarator should reject reflection");
}

void testReflectRejectsWrongAccessorParameterNames() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint i, vec2 tex, vec3 normal, mat3 basis) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-wrong-names.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "wrong accessor parameter names should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "wrong accessor parameter names should reject reflection");
}

void testReflectRejectsSingleWrongAccessorParameterName() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 tex, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-single-wrong-name.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "single wrong accessor parameter name should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "single wrong accessor parameter name should reject reflection");
}

void testReflectRejectsLegacyAccessorNormalParameterNames() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 n, mat3 tbn) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-legacy-normal-names.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "legacy n/tbn accessor parameter names should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "legacy n/tbn accessor parameter names should reject reflection");
}

void testReflectRejectsAccessorMacroWithoutFunctionDefinition() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#define lxLoadMaterialSurface(materialIndex, uv, geometricNormal, tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-macro.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "accessor macro should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "accessor macro should reject reflection");
}

void testReflectRejectsMultilineAccessorMacro() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#define MATERIAL_ACCESSOR \
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-multiline-macro.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "multiline accessor macro should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "multiline accessor macro should reject reflection");
}

void testReflectRejectsAccessorInDisabledPreprocessorBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-block.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "disabled preprocessor accessor should not satisfy Material Accessor "
         "ABI");
  EXPECT(!result.reflection.has_value(),
         "disabled preprocessor accessor should reject reflection");
}

void testReflectRejectsAccessorInCommentedDisabledPreprocessorBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0 // reason
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-comment.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "commented disabled preprocessor accessor should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "commented disabled preprocessor accessor should reject reflection");
}

void testReflectRejectsAccessorInBlockCommentedDisabledPreprocessorBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0 /* reason */
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-block-comment.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "block-commented disabled preprocessor accessor should not satisfy "
         "Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "block-commented disabled preprocessor accessor should reject "
         "reflection");
}

void testReflectRejectsAccessorAfterNestedDirectiveInDisabledBlock() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
#if 1
#endif
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-disabled-nested.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "nested disabled preprocessor accessor should not satisfy Material "
         "Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "nested disabled preprocessor accessor should reject reflection");
}

void testReflectsAccessorInElseBranchAfterDisabledIf() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxNotMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#else
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-else.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "active else branch accessor should satisfy Material Accessor ABI");
  EXPECT(result.reflection.has_value(),
         "active else branch accessor should reflect a contract");
}

void testReflectsAccessorInElifOneBranchAfterDisabledIf() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxNotMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#elif 1
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri("memory://materials/accessor-elif.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "active elif branch accessor should satisfy Material Accessor ABI");
  EXPECT(result.reflection.has_value(),
         "active elif branch accessor should reflect a contract");
}

void testReflectRejectsAccessorInElifZeroBranch() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 0
LxMaterialSurface lxNotMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#elif 0
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-elif-zero.contract.glsl"),
      source);
  EXPECT(!result.diagnostics.empty(),
         "inactive elif zero branch should not satisfy Material Accessor ABI");
  EXPECT(!result.reflection.has_value(),
         "inactive elif zero branch should reject reflection");
}

void testReflectsAccessorInIfOneBranchIgnoringInactiveElse() {
  const std::string source = R"glsl(
// LX_MATERIAL_CONTRACT_BEGIN
// type: matte
// status: supported
// reflectionHash: matte-reflect-v1
// storageAbiHash: matte-storage-v1
// accessorAbiHash: material-surface-v1
// parameter: Kd required rgb texture
// LX_MATERIAL_CONTRACT_END
#if 1
LxMaterialSurface lxLoadMaterialSurface(uint materialIndex, vec2 uv, vec3 geometricNormal, mat3 tangentFrame) { }
#else
LxMaterialSurface lxLoadMaterialSurface(uint badIndex, vec2 tex, vec3 normal, mat3 basis) { }
#endif
)glsl";
  const auto result = LX_infra::reflectMaterialContractSource(
      LX_core::ResourceUri(
          "memory://materials/accessor-if-one-else.contract.glsl"),
      source);
  EXPECT(result.diagnostics.empty(),
         "inactive else branch should not interfere with active accessor");
  EXPECT(result.reflection.has_value(),
         "active if branch accessor should reflect a contract");
}

void testValidateReflectionSetRejectsSourceSignatureConflicts() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "shared-reflect-v1";
  a.storageAbiHash = "shared-storage-v1";
  a.accessorAbiHash = "shared-accessor-v1";
  a.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});

  LX_core::MaterialContractReflection b = a;
  b.parameters.clear();
  b.parameters.push_back(LX_core::MaterialContractParameter{
      "roughness", false, {LX_core::MaterialContractParameterKind::Float}});

  const auto result =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, b});
  EXPECT(!result.diagnostics.empty(),
         "same source signature with different schemas should be diagnostic");
}

void testValidateReflectionSetRejectsAccessorAbiConflicts() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "shared-reflect-v1";
  a.storageAbiHash = "shared-storage-v1";
  a.accessorAbiHash = "shared-accessor-v1";
  a.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});

  LX_core::MaterialContractReflection b = a;
  b.accessorAbi.entryPoint = "lxLoadDifferentMaterialSurface";

  LX_core::MaterialContractReflection c = a;
  c.accessorAbi.requiredFields.push_back("clearcoat");

  const auto entryPointResult =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, b});
  EXPECT(!entryPointResult.diagnostics.empty(),
         "same source signature with different accessor entry point should be "
         "diagnostic");

  const auto requiredFieldsResult =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, c});
  EXPECT(!requiredFieldsResult.diagnostics.empty(),
         "same source signature with different accessor fields should be "
         "diagnostic");
}

void testValidateReflectionSetRejectsStorageFieldConflicts() {
  LX_core::MaterialContractReflection a;
  a.sourceUri = LX_core::ResourceUri("memory://materials/matte.contract.glsl");
  a.declaredType = "matte";
  a.reflectionHash = "shared-reflect-v1";
  a.storageAbiHash = "shared-storage-v1";
  a.accessorAbiHash = "shared-accessor-v1";
  a.parameters.push_back(LX_core::MaterialContractParameter{
      "Kd", true, {LX_core::MaterialContractParameterKind::Rgb}});
  a.storageFields.push_back(LX_core::MaterialContractStorageField{
      .name = "baseColor",
      .type = LX_core::MaterialContractStorageFieldType::Vec4,
      .inputKind = LX_core::MaterialContractStorageInputKind::ParameterValue,
      .parameterName = "Kd",
      .defaultValue = LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f},
  });

  LX_core::MaterialContractReflection b = a;
  b.storageFields[0].name = "albedo";

  const auto result =
      LX_infra::validateMaterialContractReflectionSet(std::vector{a, b});
  EXPECT(!result.diagnostics.empty(),
         "same source signature with different storage fields should be "
         "diagnostic");
}

} // namespace

int main() {
  if (std::filesystem::path sourceRoot{LXE_SOURCE_DIR}; !sourceRoot.empty()) {
    std::filesystem::current_path(sourceRoot);
  }

  testSourceSignatureIgnoresInstanceValues();
  testPackerBuildsSourceReflectedBytesRecord();
  testPackerRequiresEveryDefaultTextureSlot();
  testMaterialParserRequiresBsdfSource();
  testMaterialParserRejectsUnknownParameterFromContract();
  testMaterialParserRejectsSourceTypeMismatch();
  testMaterialParserRejectsUnsupportedContract();
  testMaterialParserRejectsBuiltInUnsupportedContracts();
  testMaterialParserAttachesReflectDiagnosticsToSource();
  testMaterialParserRejectsKindNotAllowedByContract();
  testMaterialParserStoresReflectedSourceIdentity();
  testDefaultMaterialParserRejectsMemoryContractSource();
  testSharedContractLoaderReadsBuiltInAssetSource();
  testBuiltInContractSourcesReflectActiveSchemas();
  testMaterialParserRejectsNonScalarBsdfSource();
  testMaterialParserRejectsEmptyBsdfSource();
  testMaterialParserStoresSourceIdentityAndCloneCopiesIt();
  testMaterialInstancePipelineSignatureUsesSourceSignature();
  testLegacyMaterialPipelineSignatureIgnoresInstanceValues();
  testFindParameterHitAndMiss();
  testDefaultAccessorAbi();
  testSourceSignatureIncludesStorageAndAccessorAbi();
  testMaterialSignatureIncludesPassAndRenderState();
  testReflectsContractMetadataBlock();
  testReflectsBsdfFunctionMetadata();
  testReflectRejectsBsdfFunctionWithoutAbiDefinition();
  testReflectsMaterialStorageFields();
  testReflectRejectsDuplicateStorageFieldMetadata();
  testReflectRejectsUnknownStorageFieldType();
  testReflectsAccessorWithBodyComment();
  testReflectRejectsMissingAccessor();
  testReflectRejectsMissingStatus();
  testReflectRejectsDuplicateTypeMetadata();
  testReflectRejectsDuplicateStatusMetadata();
  testReflectRejectsDuplicateReflectionHashMetadata();
  testReflectRejectsDuplicateStorageAbiHashMetadata();
  testReflectRejectsDuplicateAccessorAbiHashMetadata();
  testReflectRejectsDuplicateParameterMetadata();
  testReflectRejectsBareMetadataLines();
  testReflectRejectsCommentedOutAccessor();
  testReflectRejectsAccessorCallWithoutDefinition();
  testReflectRejectsAccessorPrototypeWithoutBody();
  testReflectRejectsWrongAccessorReturnType();
  testReflectRejectsQualifiedAccessorReturnType();
  testReflectRejectsPrefixedAccessorReturnType();
  testReflectRejectsCommentBetweenAccessorReturnAndName();
  testReflectRejectsWrongAccessorFunctionName();
  testReflectRejectsWrongAccessorParameterList();
  testReflectRejectsExtraAccessorParameter();
  testReflectRejectsWrongAccessorParameterType();
  testReflectRejectsAccessorArrayDeclarator();
  testReflectRejectsAccessorNumericParameterName();
  testReflectRejectsCommentInsideAccessorParameter();
  testReflectRejectsCommentBetweenAccessorParametersAndBody();
  testReflectRejectsAccessorPointerDeclarator();
  testReflectRejectsAccessorReferenceDeclarator();
  testReflectRejectsAccessorQualifierDeclarator();
  testReflectRejectsWrongAccessorParameterNames();
  testReflectRejectsSingleWrongAccessorParameterName();
  testReflectRejectsLegacyAccessorNormalParameterNames();
  testReflectRejectsAccessorMacroWithoutFunctionDefinition();
  testReflectRejectsMultilineAccessorMacro();
  testReflectRejectsAccessorInDisabledPreprocessorBlock();
  testReflectRejectsAccessorInCommentedDisabledPreprocessorBlock();
  testReflectRejectsAccessorInBlockCommentedDisabledPreprocessorBlock();
  testReflectRejectsAccessorAfterNestedDirectiveInDisabledBlock();
  testReflectsAccessorInElseBranchAfterDisabledIf();
  testReflectsAccessorInElifOneBranchAfterDisabledIf();
  testReflectRejectsAccessorInElifZeroBranch();
  testReflectsAccessorInIfOneBranchIgnoringInactiveElse();
  testValidateReflectionSetRejectsSourceSignatureConflicts();
  testValidateReflectionSetRejectsAccessorAbiConflicts();
  testValidateReflectionSetRejectsStorageFieldConflicts();
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
