#pragma once

#include "core/scene/ibl_bake_manifest.hpp"

#include <filesystem>
#include <optional>
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
