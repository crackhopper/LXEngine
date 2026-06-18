#pragma once

#include "core/asset/texture.hpp"
#include "core/scene/ibl_bake_manifest.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace LX_infra {

template <typename Manifest> struct ParsedIblBakeManifest final {
  std::optional<Manifest> manifest;
  std::vector<std::string> diagnostics;
};

struct ParsedSh9IrradiancePayload final {
  std::optional<LX_core::Sh9IrradiancePayload> payload;
  std::vector<std::string> diagnostics;
};

struct ParsedIblBakeKtx2Payload final {
  struct Info final {
    LX_core::TextureFormat format = LX_core::TextureFormat::RGBA8;
    u32 width = 0;
    u32 height = 0;
    u32 mipLevels = 0;
    LX_core::TextureDimension dimension = LX_core::TextureDimension::Texture2D;
    u32 faceCount = 1;
  };

  std::optional<Info> info;
  std::vector<std::string> diagnostics;
};

struct AtomicCommitResult final {
  bool ok = false;
  std::vector<std::string> diagnostics;
};

class IblBakeManifestParser final {
public:
  [[nodiscard]] ParsedIblBakeManifest<LX_core::EnvironmentIblBakeManifest>
  parseEnvironmentManifest(const LX_core::ResourceUri &uri,
                           std::string_view yamlText) const;
  [[nodiscard]] ParsedIblBakeManifest<LX_core::MaterialIblBakeManifest>
  parseMaterialManifest(const LX_core::ResourceUri &uri,
                        std::string_view yamlText) const;
  [[nodiscard]] ParsedSh9IrradiancePayload
  parseSh9IrradiancePayload(const LX_core::ResourceUri &uri,
                            std::string_view yamlText) const;
  [[nodiscard]] ParsedIblBakeKtx2Payload
  parseKtx2Payload(const LX_core::ResourceUri &uri,
                   std::span<const u8> bytes) const;

  [[nodiscard]] std::string writeEnvironmentManifest(
      const LX_core::EnvironmentIblBakeManifest &manifest) const;
  [[nodiscard]] std::string writeMaterialManifest(
      const LX_core::MaterialIblBakeManifest &manifest) const;
  [[nodiscard]] std::string
  writeSh9IrradiancePayload(const LX_core::Sh9IrradiancePayload &payload) const;

  [[nodiscard]] LX_core::IblBakeValidationResult
  validateEnvironmentPayloadFiles(
      const std::filesystem::path &manifestPath,
      const LX_core::EnvironmentIblBakeManifest &manifest) const;
  [[nodiscard]] LX_core::IblBakeValidationResult validateMaterialPayloadFiles(
      const std::filesystem::path &manifestPath,
      const LX_core::MaterialIblBakeManifest &manifest) const;

  [[nodiscard]] AtomicCommitResult
  writeAtomically(const std::filesystem::path &finalPath,
                  std::string_view contents) const;
  [[nodiscard]] AtomicCommitResult writeEnvironmentManifestAtomically(
      const std::filesystem::path &finalPath,
      const LX_core::EnvironmentIblBakeManifest &manifest) const;
};

} // namespace LX_infra
