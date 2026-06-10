#include "core/asset/material_parameter_envelope.hpp"
#include "core/asset/material_surface_schema.hpp"
#include "core/scene/scene_resource_table.hpp"
#include "infra/material_loader/material_resource_parser.hpp"

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <string_view>

using namespace LX_core;

namespace {

int g_failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

const MaterialParameterSchema *findParameter(const MaterialSurfaceSchema &schema,
                                             std::string_view name) {
    const auto it = std::find_if(
        schema.parameters.begin(), schema.parameters.end(),
        [name](const MaterialParameterSchema &parameter) {
            return parameter.name == name;
        });
    return it == schema.parameters.end() ? nullptr : &*it;
}

void expectHasParameter(const MaterialSurfaceSchema &schema,
                        std::string_view parameterName) {
    expect(findParameter(schema, parameterName) != nullptr,
           std::string(schema.bsdfType) + " should expose parameter " +
               std::string(parameterName));
}

void expectAllows(const MaterialSurfaceSchema &schema, std::string_view parameterName,
                  MaterialEnvelopeKind kind) {
    const MaterialParameterSchema *parameter = findParameter(schema, parameterName);
    expect(parameter != nullptr,
           std::string(schema.bsdfType) + " should expose parameter " +
               std::string(parameterName));
    if (parameter == nullptr) {
        return;
    }

    expect(std::find(parameter->allowedKinds.begin(), parameter->allowedKinds.end(),
                     kind) != parameter->allowedKinds.end(),
           std::string(schema.bsdfType) + "." + std::string(parameterName) +
               " should allow requested envelope kind");
}

void expectSchema(std::string_view bsdfType,
                  std::initializer_list<std::string_view> parameterNames) {
    const MaterialSurfaceSchema *schema = findMaterialSurfaceSchema(bsdfType);
    expect(schema != nullptr, std::string("missing BSDF schema ") +
                                std::string(bsdfType));
    if (schema == nullptr) {
        return;
    }

    for (std::string_view parameterName : parameterNames) {
        expectHasParameter(*schema, parameterName);
    }
}

void testRequiredBsdfSchemas() {
    expectSchema("matte", {"Kd", "sigma"});
    expectSchema("glass", {"Kr", "Kt", "eta", "uroughness", "vroughness"});
    expectSchema("uber", {"Kd", "Ks", "Kr", "Kt", "opacity", "eta"});
    expectSchema("metal", {"eta", "k"});
    expectSchema("substrate", {"Kd", "Ks", "uroughness", "vroughness"});
    expectSchema("fourier", {"bsdffile"});
    expectSchema("mix", {"namedmaterial1", "namedmaterial2", "amount"});
}

void testPbrtEnvelopeKindContracts() {
    const MaterialSurfaceSchema *matte = findMaterialSurfaceSchema("matte");
    const MaterialSurfaceSchema *metal = findMaterialSurfaceSchema("metal");
    const MaterialSurfaceSchema *fourier = findMaterialSurfaceSchema("fourier");
    const MaterialSurfaceSchema *mix = findMaterialSurfaceSchema("mix");

    expect(matte != nullptr, "matte schema should exist");
    expect(metal != nullptr, "metal schema should exist");
    expect(fourier != nullptr, "fourier schema should exist");
    expect(mix != nullptr, "mix schema should exist");
    if (matte == nullptr || metal == nullptr || fourier == nullptr ||
        mix == nullptr) {
        return;
    }

    expectAllows(*matte, "Kd", MaterialEnvelopeKind::Rgb);
    expectAllows(*matte, "Kd", MaterialEnvelopeKind::Texture);
    expectAllows(*matte, "sigma", MaterialEnvelopeKind::Float);
    expectAllows(*metal, "eta", MaterialEnvelopeKind::Spectrum);
    expectAllows(*metal, "k", MaterialEnvelopeKind::Spectrum);
    expectAllows(*fourier, "bsdffile", MaterialEnvelopeKind::BsdfTable);
    expectAllows(*mix, "namedmaterial1", MaterialEnvelopeKind::MaterialRef);
    expectAllows(*mix, "namedmaterial2", MaterialEnvelopeKind::MaterialRef);
    expectAllows(*mix, "amount", MaterialEnvelopeKind::Float);
}

void testEnvelopeShapeValidation() {
    MaterialParameterEnvelope floatEnvelope;
    floatEnvelope.kind = MaterialEnvelopeKind::Float;
    floatEnvelope.floatValue = 0.5f;
    expect(hasInlineValue(floatEnvelope), "float envelope should report inline value");
    expect(!hasResourceUri(floatEnvelope), "float envelope should not report uri");
    expect(validateEnvelopeShape(floatEnvelope).empty(),
           "inline float envelope should be valid");

    MaterialParameterEnvelope textureEnvelope;
    textureEnvelope.kind = MaterialEnvelopeKind::Texture;
    textureEnvelope.valueType = MaterialEnvelopeValueType::Rgb;
    textureEnvelope.uri = "textures/albedo.png";
    expect(!hasInlineValue(textureEnvelope),
           "texture envelope should not report inline value");
    expect(hasResourceUri(textureEnvelope), "texture envelope should report uri");
    expect(validateEnvelopeShape(textureEnvelope).empty(),
           "texture envelope with value type and uri should be valid");

    MaterialParameterEnvelope ambiguousEnvelope = floatEnvelope;
    ambiguousEnvelope.uri = "textures/invalid.png";
    expect(!validateEnvelopeShape(ambiguousEnvelope).empty(),
           "envelope cannot provide inline value and uri at the same time");

    MaterialParameterEnvelope missingTextureUri;
    missingTextureUri.kind = MaterialEnvelopeKind::Texture;
    missingTextureUri.valueType = MaterialEnvelopeValueType::Rgb;
    expect(!validateEnvelopeShape(missingTextureUri).empty(),
           "texture envelope should require uri");

    MaterialParameterEnvelope typelessTexture = textureEnvelope;
    typelessTexture.valueType = MaterialEnvelopeValueType::None;
    expect(!validateEnvelopeShape(typelessTexture).empty(),
           "texture envelope should declare the sampled value type");

    MaterialParameterEnvelope tableEnvelope;
    tableEnvelope.kind = MaterialEnvelopeKind::BsdfTable;
    tableEnvelope.uri = "assets/bsdf/fabric.bsdf";
    expect(validateEnvelopeShape(tableEnvelope).empty(),
           "Fourier BSDF table envelope should be valid with uri");

    MaterialParameterEnvelope materialRefEnvelope;
    materialRefEnvelope.kind = MaterialEnvelopeKind::MaterialRef;
    materialRefEnvelope.uri = "named:paint_clearcoat";
    expect(validateEnvelopeShape(materialRefEnvelope).empty(),
           "mix material references should be valid with uri");
}

void expectParses(std::string_view label, std::string_view yamlText) {
    LX_core::SceneResourceTable table;
    LX_infra::MaterialResourceParser parser;
    const auto parsed =
        parser.parse(table, std::string("memory://") + std::string(label),
                     yamlText);
    expect(parsed.instance != nullptr,
           std::string(label) + " should produce a material instance");
    expect(parsed.diagnostics.empty(),
           std::string(label) + " should parse without diagnostics");
}

void testParserAcceptsMinimalRequiredBsdfs() {
    expectParses("matte", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }
    sigma: { kind: float, value: 0.0 }
)");

    expectParses("glass", R"(
schema: lxe.material.v2
bsdf:
  type: glass
  parameters:
    Kr: { kind: rgb, value: [1.0, 1.0, 1.0] }
    Kt: { kind: rgb, value: [1.0, 1.0, 1.0] }
    eta: { kind: float, value: 1.5 }
    uroughness: { kind: float, value: 0.0 }
    vroughness: { kind: float, value: 0.0 }
)");

    expectParses("uber", R"(
schema: lxe.material.v2
bsdf:
  type: uber
  parameters:
    Kd: { kind: rgb, value: [0.5, 0.5, 0.5] }
    Ks: { kind: rgb, value: [0.2, 0.2, 0.2] }
)");

    expectParses("metal", R"(
schema: lxe.material.v2
bsdf:
  type: metal
  parameters:
    eta: { kind: spectrum, uri: spectra/copper_eta.spd }
    k: { kind: spectrum, uri: spectra/copper_k.spd }
)");

    expectParses("substrate", R"(
schema: lxe.material.v2
bsdf:
  type: substrate
  parameters:
    Kd: { kind: rgb, value: [0.4, 0.3, 0.2] }
    Ks: { kind: rgb, value: [0.1, 0.1, 0.1] }
    uroughness: { kind: float, value: 0.25 }
    vroughness: { kind: float, value: 0.35 }
)");

    expectParses("fourier", R"(
schema: lxe.material.v2
bsdf:
  type: fourier
  parameters:
    bsdffile: { kind: bsdfTable, uri: bsdf/fabric.bsdf }
)");

    expectParses("mix", R"(
schema: lxe.material.v2
bsdf:
  type: mix
  parameters:
    namedmaterial1: { kind: materialRef, uri: named:matte_base }
    namedmaterial2: { kind: materialRef, uri: named:clearcoat }
    amount: { kind: float, value: 0.35 }
)");
}

void testParserRejectsInvalidEnvelopeInputs() {
    LX_core::SceneResourceTable table;
    LX_infra::MaterialResourceParser parser;

    const auto missingRequired = parser.parse(table, "memory://missing-required", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6] }
)");
    expect(missingRequired.instance == nullptr,
           "missing required BSDF parameter should fail");
    expect(!missingRequired.diagnostics.empty(),
           "missing required BSDF parameter should emit diagnostics");

    const auto ambiguous = parser.parse(table, "memory://ambiguous", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: rgb, value: [0.8, 0.7, 0.6], uri: textures/kd.png }
    sigma: { kind: float, value: 0.0 }
)");
    expect(ambiguous.instance == nullptr,
           "inline value plus uri should fail");
    expect(!ambiguous.diagnostics.empty(),
           "inline value plus uri should emit diagnostics");

    const auto textureMissingValueType = parser.parse(table, "memory://texture", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: texture, uri: textures/kd.png }
    sigma: { kind: float, value: 0.0 }
)");
    expect(textureMissingValueType.instance == nullptr,
           "texture envelope missing valueType should fail");
    expect(!textureMissingValueType.diagnostics.empty(),
           "texture envelope missing valueType should emit diagnostics");

    const auto nestedMix = parser.parse(table, "memory://mix-nested", R"(
schema: lxe.material.v2
bsdf:
  type: mix
  parameters:
    namedmaterial1: { kind: materialRef, uri: named:another_mix, bsdfType: mix }
    namedmaterial2: { kind: materialRef, uri: named:matte_base }
    amount: { kind: float, value: 0.5 }
)");
    expect(nestedMix.instance == nullptr,
           "mix child material declared as mix should fail");
    expect(!nestedMix.diagnostics.empty(),
           "mix child material declared as mix should emit diagnostics");
}

void testParserStoresEnvelopeTruthAndDependencies() {
    LX_core::SceneResourceTable table;
    LX_infra::MaterialResourceParser parser;
    const auto parsed = parser.parse(table, "memory://resource-deps", R"(
schema: lxe.material.v2
bsdf:
  type: matte
  parameters:
    Kd: { kind: texture, valueType: rgb, uri: textures/kd.png }
    sigma: { kind: float, value: 0.0 }
)");

    expect(parsed.instance != nullptr,
           "valid texture material should produce instance");
    if (!parsed.instance) {
        return;
    }

    const auto kd =
        parsed.instance->getMaterialEnvelope(LX_core::StringID("Kd"));
    expect(kd.has_value(), "MaterialInstance should store Kd envelope");
    expect(kd && kd->get().kind == MaterialEnvelopeKind::Texture,
           "Kd envelope should retain texture kind");
    expect(kd && kd->get().uri == "textures/kd.png",
           "Kd envelope should retain texture uri");
    expect(parsed.dependencies.size() == 1,
           "texture envelope should produce one resource dependency");
    expect(!parsed.dependencies.empty() &&
               parsed.dependencies.front().uri.string() == "textures/kd.png",
           "resource dependency should retain texture uri");
}

} // namespace

int main() {
    testRequiredBsdfSchemas();
    testPbrtEnvelopeKindContracts();
    testEnvelopeShapeValidation();
    testParserAcceptsMinimalRequiredBsdfs();
    testParserRejectsInvalidEnvelopeInputs();
    testParserStoresEnvelopeTruthAndDependencies();

    if (g_failures != 0) {
        std::cerr << g_failures << " material v2 parser checks failed\n";
        return 1;
    }
    return 0;
}
