#include "core/asset/material_surface_schema.hpp"

#include <array>

namespace LX_core {
namespace {

using Kind = MaterialEnvelopeKind;

MaterialParameterSchema parameter(std::string name,
                                  std::vector<Kind> allowedKinds,
                                  bool required = true) {
  return MaterialParameterSchema{std::move(name), std::move(allowedKinds),
                                 required};
}

MaterialParameterSchema normalMapParameter() {
  return parameter("normalmap", {Kind::Texture}, false);
}

const std::array<MaterialSurfaceSchema, 7> &surfaceSchemas() {
  static const std::array<MaterialSurfaceSchema, 7> schemas = {
      MaterialSurfaceSchema{
          "matte",
          {
              parameter("Kd", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("sigma", {Kind::Float, Kind::Texture}),
              normalMapParameter(),
          },
      },
      MaterialSurfaceSchema{
          "glass",
          {
              parameter("Kr", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("Kt", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("eta", {Kind::Float, Kind::Texture}),
              parameter("uroughness", {Kind::Float, Kind::Texture}),
              parameter("vroughness", {Kind::Float, Kind::Texture}),
              normalMapParameter(),
          },
      },
      MaterialSurfaceSchema{
          "uber",
          {
              parameter("Kd", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("Ks", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("Kr", {Kind::Rgb, Kind::Texture, Kind::Spectrum},
                        false),
              parameter("Kt", {Kind::Rgb, Kind::Texture, Kind::Spectrum},
                        false),
              parameter("opacity", {Kind::Rgb, Kind::Texture}, false),
              parameter("eta", {Kind::Float, Kind::Texture}, false),
              parameter("uroughness", {Kind::Float, Kind::Texture}, false),
              parameter("vroughness", {Kind::Float, Kind::Texture}, false),
              normalMapParameter(),
          },
      },
      MaterialSurfaceSchema{
          "metal",
          {
              parameter("eta", {Kind::Spectrum}),
              parameter("k", {Kind::Spectrum}),
              parameter("uroughness", {Kind::Float, Kind::Texture}, false),
              parameter("vroughness", {Kind::Float, Kind::Texture}, false),
              normalMapParameter(),
          },
      },
      MaterialSurfaceSchema{
          "substrate",
          {
              parameter("Kd", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("Ks", {Kind::Rgb, Kind::Texture, Kind::Spectrum}),
              parameter("uroughness", {Kind::Float, Kind::Texture}),
              parameter("vroughness", {Kind::Float, Kind::Texture}),
              normalMapParameter(),
          },
      },
      MaterialSurfaceSchema{
          "fourier",
          {
              parameter("bsdffile", {Kind::BsdfTable}),
              normalMapParameter(),
          },
      },
      MaterialSurfaceSchema{
          "mix",
          {
              parameter("namedmaterial1", {Kind::MaterialRef}),
              parameter("namedmaterial2", {Kind::MaterialRef}),
              parameter("amount", {Kind::Float}),
              normalMapParameter(),
          },
      },
  };
  return schemas;
}

} // namespace

const MaterialSurfaceSchema *
findMaterialSurfaceSchema(std::string_view bsdfType) {
  for (const MaterialSurfaceSchema &schema : surfaceSchemas()) {
    if (schema.bsdfType == bsdfType) {
      return &schema;
    }
  }
  return nullptr;
}

} // namespace LX_core
