#include "infra/material_loader/material_contract_reflector.hpp"

#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace LX_infra {
namespace {

[[nodiscard]] std::string trim(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }

  std::size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
    --last;
  }

  return std::string(text.substr(first, last - first));
}

[[nodiscard]] std::optional<LX_core::MaterialContractParameterKind>
kindFromToken(const std::string &token) {
  if (token == "float") {
    return LX_core::MaterialContractParameterKind::Float;
  }
  if (token == "rgb") {
    return LX_core::MaterialContractParameterKind::Rgb;
  }
  if (token == "spectrum") {
    return LX_core::MaterialContractParameterKind::Spectrum;
  }
  if (token == "texture") {
    return LX_core::MaterialContractParameterKind::Texture;
  }
  if (token == "integer") {
    return LX_core::MaterialContractParameterKind::Integer;
  }
  if (token == "bool") {
    return LX_core::MaterialContractParameterKind::Bool;
  }
  if (token == "string") {
    return LX_core::MaterialContractParameterKind::String;
  }
  if (token == "materialRef") {
    return LX_core::MaterialContractParameterKind::MaterialRef;
  }
  if (token == "bsdfTable") {
    return LX_core::MaterialContractParameterKind::BsdfTable;
  }
  return std::nullopt;
}

[[nodiscard]] bool isIdentifierChar(char c) {
  const auto ch = static_cast<unsigned char>(c);
  return std::isalnum(ch) != 0 || c == '_';
}

[[nodiscard]] std::optional<std::string>
commentMetadataLine(std::string_view line) {
  const std::string stripped = trim(line);
  if (stripped.rfind("//", 0) != 0) {
    return std::nullopt;
  }
  return trim(std::string_view(stripped).substr(2));
}

[[nodiscard]] bool hasEntryPointDefinition(std::string_view sourceText,
                                           std::string_view entryPoint) {
  std::string code;
  code.reserve(sourceText.size());

  bool inBlockComment = false;
  for (std::size_t i = 0; i < sourceText.size();) {
    if (inBlockComment) {
      if (i + 1 < sourceText.size() && sourceText[i] == '*' &&
          sourceText[i + 1] == '/') {
        inBlockComment = false;
        code.append(2, ' ');
        i += 2;
      } else {
        code.push_back(sourceText[i] == '\n' ? '\n' : ' ');
        ++i;
      }
      continue;
    }

    if (i + 1 < sourceText.size() && sourceText[i] == '/' &&
        sourceText[i + 1] == '/') {
      code.append(2, ' ');
      i += 2;
      while (i < sourceText.size() && sourceText[i] != '\n') {
        code.push_back(' ');
        ++i;
      }
      continue;
    }

    if (i + 1 < sourceText.size() && sourceText[i] == '/' &&
        sourceText[i + 1] == '*') {
      inBlockComment = true;
      code.append(2, ' ');
      i += 2;
      continue;
    }

    code.push_back(sourceText[i]);
    ++i;
  }

  std::size_t pos = code.find(entryPoint);
  while (pos != std::string::npos) {
    const bool tokenStart = pos == 0 || !isIdentifierChar(code[pos - 1]);
    const std::size_t afterToken = pos + entryPoint.size();
    const bool tokenEnd =
        afterToken >= code.size() || !isIdentifierChar(code[afterToken]);
    if (tokenStart && tokenEnd) {
      std::size_t next = afterToken;
      while (next < code.size() &&
             std::isspace(static_cast<unsigned char>(code[next])) != 0) {
        ++next;
      }
      if (next < code.size() && code[next] == '(') {
        int depth = 1;
        ++next;
        while (next < code.size() && depth > 0) {
          if (code[next] == '(') {
            ++depth;
          } else if (code[next] == ')') {
            --depth;
          }
          ++next;
        }
        if (depth == 0) {
          while (next < code.size() &&
                 std::isspace(static_cast<unsigned char>(code[next])) != 0) {
            ++next;
          }
          if (next < code.size() && code[next] == '{') {
            return true;
          }
        }
      }
    }
    pos = code.find(entryPoint, pos + 1);
  }
  return false;
}

} // namespace

MaterialContractReflectionResult
reflectMaterialContractSource(const LX_core::ResourceUri &sourceUri,
                              std::string_view sourceText) {
  MaterialContractReflectionResult result;
  LX_core::MaterialContractReflection reflection;
  reflection.sourceUri = sourceUri;

  bool inBlock = false;
  bool sawBlock = false;
  bool sawStatus = false;
  std::istringstream input{std::string(sourceText)};
  std::string line;
  while (std::getline(input, line)) {
    const std::optional<std::string> maybeStripped = commentMetadataLine(line);
    if (!maybeStripped.has_value()) {
      continue;
    }

    const std::string &stripped = *maybeStripped;
    if (stripped == "LX_MATERIAL_CONTRACT_BEGIN") {
      inBlock = true;
      sawBlock = true;
      continue;
    }
    if (stripped == "LX_MATERIAL_CONTRACT_END") {
      inBlock = false;
      continue;
    }
    if (!inBlock || stripped.empty()) {
      continue;
    }

    if (stripped.rfind("type:", 0) == 0) {
      reflection.declaredType = trim(std::string_view(stripped).substr(5));
    } else if (stripped.rfind("status:", 0) == 0) {
      sawStatus = true;
      const std::string status = trim(std::string_view(stripped).substr(7));
      if (status == "supported") {
        reflection.supportStatus =
            LX_core::MaterialContractSupportStatus::Supported;
      } else if (status == "unsupported") {
        reflection.supportStatus =
            LX_core::MaterialContractSupportStatus::Unsupported;
      } else {
        result.diagnostics.push_back(
            sourceUri.string() + ": unknown support status '" + status + "'");
      }
    } else if (stripped.rfind("reflectionHash:", 0) == 0) {
      reflection.reflectionHash = trim(std::string_view(stripped).substr(15));
    } else if (stripped.rfind("storageAbiHash:", 0) == 0) {
      reflection.storageAbiHash = trim(std::string_view(stripped).substr(15));
    } else if (stripped.rfind("accessorAbiHash:", 0) == 0) {
      reflection.accessorAbiHash = trim(std::string_view(stripped).substr(16));
    } else if (stripped.rfind("parameter:", 0) == 0) {
      std::istringstream tokens{trim(std::string_view(stripped).substr(10))};
      LX_core::MaterialContractParameter parameter;
      std::string requiredToken;
      tokens >> parameter.name >> requiredToken;
      if (parameter.name.empty()) {
        result.diagnostics.push_back(sourceUri.string() +
                                     ": parameter is missing a name");
        continue;
      }
      if (requiredToken == "required") {
        parameter.required = true;
      } else if (requiredToken == "optional") {
        parameter.required = false;
      } else {
        result.diagnostics.push_back(sourceUri.string() + ": parameter '" +
                                     parameter.name +
                                     "' must be required or optional");
      }

      std::string kindToken;
      while (tokens >> kindToken) {
        const auto kind = kindFromToken(kindToken);
        if (!kind.has_value()) {
          result.diagnostics.push_back(sourceUri.string() +
                                       ": unknown parameter kind '" +
                                       kindToken + "'");
          continue;
        }
        parameter.allowedKinds.push_back(*kind);
      }
      if (parameter.allowedKinds.empty()) {
        result.diagnostics.push_back(sourceUri.string() + ": parameter '" +
                                     parameter.name +
                                     "' must declare at least one kind");
      }
      reflection.parameters.push_back(std::move(parameter));
    } else {
      result.diagnostics.push_back(sourceUri.string() +
                                   ": unknown contract metadata line '" +
                                   stripped + "'");
    }
  }

  if (!sawBlock) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": missing LX_MATERIAL_CONTRACT block");
  }
  if (inBlock) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": unterminated LX_MATERIAL_CONTRACT block");
  }
  if (reflection.declaredType.empty()) {
    result.diagnostics.push_back(sourceUri.string() + ": missing type");
  }
  if (!sawStatus) {
    result.diagnostics.push_back(sourceUri.string() + ": missing status");
  }
  if (reflection.reflectionHash.empty() || reflection.storageAbiHash.empty() ||
      reflection.accessorAbiHash.empty()) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": missing reflection/storage/accessor hash");
  }
  if (!hasEntryPointDefinition(sourceText, reflection.accessorAbi.entryPoint)) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": missing Material Accessor ABI entry");
  }
  if (reflection.parameters.empty()) {
    result.diagnostics.push_back(sourceUri.string() +
                                 ": contract must declare parameters");
  }

  if (result.diagnostics.empty()) {
    result.reflection = std::move(reflection);
  }
  return result;
}

} // namespace LX_infra
