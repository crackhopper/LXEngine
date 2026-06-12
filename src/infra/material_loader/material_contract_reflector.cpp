#include "infra/material_loader/material_contract_reflector.hpp"

#include "core/utils/filesystem_tools.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

[[nodiscard]] bool hasUriScheme(const LX_core::ResourceUri &uri) {
  return uri.string().find("://") != std::string::npos;
}

[[nodiscard]] std::filesystem::path
filesystemPathForContractSource(const LX_core::ResourceUri &sourceUri) {
  constexpr std::string_view assetsPrefix = "assets://";
  const std::string &source = sourceUri.string();
  if (source.rfind(assetsPrefix, 0) == 0) {
    return resolveRuntimePath(std::filesystem::path("assets") /
                              source.substr(assetsPrefix.size()));
  }

  std::filesystem::path path(source);
  if (path.is_absolute() || std::filesystem::exists(path)) {
    return path;
  }
  return resolveRuntimePath(path);
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

[[nodiscard]] bool isIdentifierStart(char c) {
  const auto ch = static_cast<unsigned char>(c);
  return std::isalpha(ch) != 0 || c == '_';
}

[[nodiscard]] std::optional<std::string>
commentMetadataLine(std::string_view line) {
  const std::string stripped = trim(line);
  if (stripped.rfind("//", 0) != 0) {
    return std::nullopt;
  }
  return trim(std::string_view(stripped).substr(2));
}

[[nodiscard]] bool lineContinuesPreprocessorDirective(std::string_view line) {
  std::size_t pos = line.size();
  while (pos > 0 && line[pos - 1] != '\n' &&
         std::isspace(static_cast<unsigned char>(line[pos - 1])) != 0) {
    --pos;
  }
  return pos > 0 && line[pos - 1] == '\\';
}

[[nodiscard]] std::string stripLineAndBlockComments(std::string_view text) {
  std::string result;
  result.reserve(text.size());

  bool inBlockComment = false;
  for (std::size_t i = 0; i < text.size();) {
    if (inBlockComment) {
      if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '/') {
        inBlockComment = false;
        i += 2;
      } else {
        ++i;
      }
      continue;
    }

    if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
      break;
    }
    if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
      inBlockComment = true;
      i += 2;
      continue;
    }

    result.push_back(text[i]);
    ++i;
  }

  return result;
}

[[nodiscard]] std::string directiveKeyword(std::string_view directive) {
  const std::string stripped = trim(stripLineAndBlockComments(directive));
  if (stripped.empty() || stripped[0] != '#') {
    return "";
  }

  std::size_t pos = 1;
  while (pos < stripped.size() &&
         std::isspace(static_cast<unsigned char>(stripped[pos])) != 0) {
    ++pos;
  }
  const std::size_t begin = pos;
  while (pos < stripped.size() && isIdentifierChar(stripped[pos])) {
    ++pos;
  }
  return stripped.substr(begin, pos - begin);
}

[[nodiscard]] bool directiveStartsDisabledBlock(std::string_view directive) {
  const std::string stripped = trim(stripLineAndBlockComments(directive));
  if (stripped.empty() || stripped[0] != '#') {
    return false;
  }

  std::size_t pos = 1;
  while (pos < stripped.size() &&
         std::isspace(static_cast<unsigned char>(stripped[pos])) != 0) {
    ++pos;
  }
  if (stripped.compare(pos, 2, "if") != 0 ||
      (pos + 2 < stripped.size() && isIdentifierChar(stripped[pos + 2]))) {
    return false;
  }
  pos += 2;
  while (pos < stripped.size() &&
         std::isspace(static_cast<unsigned char>(stripped[pos])) != 0) {
    ++pos;
  }
  return pos < stripped.size() && stripped[pos] == '0' &&
         (pos + 1 == stripped.size() || !isIdentifierChar(stripped[pos + 1]));
}

[[nodiscard]] bool directiveConditionIsActive(std::string_view directive) {
  const std::string stripped = trim(stripLineAndBlockComments(directive));
  if (stripped.empty() || stripped[0] != '#') {
    return true;
  }

  std::size_t pos = 1;
  while (pos < stripped.size() &&
         std::isspace(static_cast<unsigned char>(stripped[pos])) != 0) {
    ++pos;
  }
  while (pos < stripped.size() && isIdentifierChar(stripped[pos])) {
    ++pos;
  }
  while (pos < stripped.size() &&
         std::isspace(static_cast<unsigned char>(stripped[pos])) != 0) {
    ++pos;
  }

  if (pos < stripped.size() && stripped[pos] == '0' &&
      (pos + 1 == stripped.size() || !isIdentifierChar(stripped[pos + 1]))) {
    return false;
  }
  return true;
}

struct ConditionalFrame final {
  bool parentActive = true;
  bool branchActive = true;
  bool branchTaken = false;
};

[[nodiscard]] bool
preprocessorIsActive(const std::vector<ConditionalFrame> &conditionalStack) {
  return conditionalStack.empty() || conditionalStack.back().branchActive;
}

[[nodiscard]] std::optional<std::size_t>
expectedReturnTypeBegin(const std::string &code, std::size_t entryPointPos) {
  std::size_t end = entryPointPos;
  while (end > 0 &&
         std::isspace(static_cast<unsigned char>(code[end - 1])) != 0) {
    --end;
  }

  std::size_t begin = end;
  while (begin > 0 && isIdentifierChar(code[begin - 1])) {
    --begin;
  }
  if (code.substr(begin, end - begin) != "LxMaterialSurface") {
    return std::nullopt;
  }

  std::size_t prefixEnd = begin;
  while (prefixEnd > 0 &&
         std::isspace(static_cast<unsigned char>(code[prefixEnd - 1])) != 0) {
    --prefixEnd;
  }
  if (prefixEnd != 0 && code[prefixEnd - 1] != '}' &&
      code[prefixEnd - 1] != ';') {
    return std::nullopt;
  }
  return begin;
}

[[nodiscard]] bool hasExpectedParameter(std::string_view parameter,
                                        std::string_view expectedType,
                                        std::string_view expectedName) {
  const std::string stripped = trim(parameter);
  std::size_t typeEnd = 0;
  while (typeEnd < stripped.size() && isIdentifierChar(stripped[typeEnd])) {
    ++typeEnd;
  }
  if (stripped.substr(0, typeEnd) != expectedType) {
    return false;
  }

  if (typeEnd >= stripped.size() ||
      std::isspace(static_cast<unsigned char>(stripped[typeEnd])) == 0) {
    return false;
  }

  const std::string declarator =
      trim(std::string_view(stripped).substr(typeEnd));
  if (declarator.empty()) {
    return false;
  }

  if (!isIdentifierStart(declarator[0])) {
    return false;
  }
  for (const char c : declarator) {
    if (!isIdentifierChar(c)) {
      return false;
    }
  }
  return declarator == expectedName;
}

[[nodiscard]] bool hasExpectedParameterTypes(std::string_view parameters) {
  constexpr std::array expectedTypes{"uint", "vec2", "vec3", "mat3"};
  constexpr std::array expectedNames{"materialIndex", "uv", "n", "tbn"};

  std::size_t begin = 0;
  for (std::size_t index = 0; index < expectedTypes.size(); ++index) {
    const std::size_t comma = parameters.find(',', begin);
    const std::size_t end =
        comma == std::string_view::npos ? parameters.size() : comma;
    if (!hasExpectedParameter(parameters.substr(begin, end - begin),
                              expectedTypes[index], expectedNames[index])) {
      return false;
    }
    if (index + 1 < expectedTypes.size()) {
      if (comma == std::string_view::npos) {
        return false;
      }
      begin = comma + 1;
    } else if (comma != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool hasEntryPointDefinition(std::string_view sourceText,
                                           std::string_view entryPoint) {
  std::string code;
  code.reserve(sourceText.size());
  std::string commentMask;
  commentMask.reserve(sourceText.size());

  const auto appendCode = [&code, &commentMask](char c, bool isComment) {
    code.push_back(c);
    commentMask.push_back(isComment ? '1' : '0');
  };
  const auto appendCodeCount = [&code, &commentMask](std::size_t count, char c,
                                                     bool isComment) {
    code.append(count, c);
    commentMask.append(count, isComment ? '1' : '0');
  };

  bool inBlockComment = false;
  std::vector<ConditionalFrame> conditionalStack;
  for (std::size_t i = 0; i < sourceText.size();) {
    if (inBlockComment) {
      if (i + 1 < sourceText.size() && sourceText[i] == '*' &&
          sourceText[i + 1] == '/') {
        inBlockComment = false;
        appendCodeCount(2, ' ', true);
        i += 2;
      } else {
        appendCode(sourceText[i] == '\n' ? '\n' : ' ', true);
        ++i;
      }
      continue;
    }

    if (i + 1 < sourceText.size() && sourceText[i] == '/' &&
        sourceText[i + 1] == '/') {
      appendCodeCount(2, ' ', true);
      i += 2;
      while (i < sourceText.size() && sourceText[i] != '\n') {
        appendCode(' ', true);
        ++i;
      }
      continue;
    }

    if (i + 1 < sourceText.size() && sourceText[i] == '/' &&
        sourceText[i + 1] == '*') {
      inBlockComment = true;
      appendCodeCount(2, ' ', true);
      i += 2;
      continue;
    }

    if (sourceText[i] == '#') {
      const std::size_t lineStart = code.find_last_of('\n');
      const std::size_t firstOnLine =
          lineStart == std::string::npos ? 0 : lineStart + 1;
      bool onlyWhitespaceBeforeHash = true;
      for (std::size_t j = firstOnLine; j < code.size(); ++j) {
        if (std::isspace(static_cast<unsigned char>(code[j])) == 0) {
          onlyWhitespaceBeforeHash = false;
          break;
        }
      }

      if (onlyWhitespaceBeforeHash) {
        bool continuedDirective = false;
        std::string directiveText;
        do {
          continuedDirective = false;
          std::string line;
          while (i < sourceText.size() && sourceText[i] != '\n') {
            line.push_back(sourceText[i]);
            appendCode(' ', false);
            ++i;
          }
          directiveText += line;
          continuedDirective = lineContinuesPreprocessorDirective(line);
          if (i < sourceText.size() && sourceText[i] == '\n') {
            appendCode('\n', false);
            ++i;
          }
          while (continuedDirective && i < sourceText.size() &&
                 std::isspace(static_cast<unsigned char>(sourceText[i])) != 0 &&
                 sourceText[i] != '\n') {
            appendCode(' ', false);
            ++i;
          }
        } while (continuedDirective && i < sourceText.size());

        const std::string directive = trim(directiveText);
        const std::string keyword = directiveKeyword(directive);
        if (keyword == "if" || keyword == "ifdef" || keyword == "ifndef") {
          const bool parentActive = preprocessorIsActive(conditionalStack);
          const bool conditionActive =
              keyword == "if" ? !directiveStartsDisabledBlock(directive) : true;
          conditionalStack.push_back(ConditionalFrame{
              parentActive, parentActive && conditionActive, conditionActive});
        } else if (keyword == "elif") {
          if (!conditionalStack.empty()) {
            ConditionalFrame &frame = conditionalStack.back();
            if (!frame.parentActive || frame.branchTaken) {
              frame.branchActive = false;
            } else {
              const bool conditionActive =
                  directiveConditionIsActive(directive);
              frame.branchActive = conditionActive;
              frame.branchTaken = conditionActive;
            }
          }
        } else if (keyword == "else") {
          if (!conditionalStack.empty()) {
            ConditionalFrame &frame = conditionalStack.back();
            frame.branchActive = frame.parentActive && !frame.branchTaken;
            frame.branchTaken = true;
          }
        } else if (keyword == "endif") {
          if (!conditionalStack.empty()) {
            conditionalStack.pop_back();
          }
        }
        continue;
      }
    }

    if (!preprocessorIsActive(conditionalStack)) {
      appendCode(sourceText[i] == '\n' ? '\n' : ' ', false);
      ++i;
      continue;
    }

    appendCode(sourceText[i], false);
    ++i;
  }

  std::size_t pos = code.find(entryPoint);
  while (pos != std::string::npos) {
    const bool tokenStart = pos == 0 || !isIdentifierChar(code[pos - 1]);
    const std::size_t afterToken = pos + entryPoint.size();
    const bool tokenEnd =
        afterToken >= code.size() || !isIdentifierChar(code[afterToken]);
    const std::optional<std::size_t> returnBegin =
        expectedReturnTypeBegin(code, pos);
    if (tokenStart && tokenEnd && returnBegin.has_value()) {
      std::size_t next = afterToken;
      while (next < code.size() &&
             std::isspace(static_cast<unsigned char>(code[next])) != 0) {
        ++next;
      }
      if (next < code.size() && code[next] == '(') {
        const std::size_t parameterBegin = next + 1;
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
          const std::size_t parameterEnd = next - 1;
          const std::size_t firstComment = commentMask.find('1', *returnBegin);
          if (!hasExpectedParameterTypes(std::string_view(code).substr(
                  parameterBegin, parameterEnd - parameterBegin))) {
            pos = code.find(entryPoint, pos + 1);
            continue;
          }
          while (next < code.size() &&
                 std::isspace(static_cast<unsigned char>(code[next])) != 0) {
            ++next;
          }
          if (firstComment != std::string::npos && firstComment < next) {
            pos = code.find(entryPoint, pos + 1);
            continue;
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

[[nodiscard]] bool
sameParameter(const LX_core::MaterialContractParameter &lhs,
              const LX_core::MaterialContractParameter &rhs) {
  return lhs.name == rhs.name && lhs.required == rhs.required &&
         lhs.allowedKinds == rhs.allowedKinds;
}

[[nodiscard]] bool
sameAccessorAbi(const LX_core::MaterialContractAccessorAbi &lhs,
                const LX_core::MaterialContractAccessorAbi &rhs) {
  return lhs.entryPoint == rhs.entryPoint &&
         lhs.requiredFields == rhs.requiredFields;
}

[[nodiscard]] bool
sameContractLayout(const LX_core::MaterialContractReflection &lhs,
                   const LX_core::MaterialContractReflection &rhs) {
  if (lhs.declaredType != rhs.declaredType ||
      lhs.supportStatus != rhs.supportStatus ||
      lhs.reflectionHash != rhs.reflectionHash ||
      lhs.storageAbiHash != rhs.storageAbiHash ||
      lhs.accessorAbiHash != rhs.accessorAbiHash ||
      !sameAccessorAbi(lhs.accessorAbi, rhs.accessorAbi) ||
      lhs.parameters.size() != rhs.parameters.size()) {
    return false;
  }

  for (std::size_t i = 0; i < lhs.parameters.size(); ++i) {
    if (!sameParameter(lhs.parameters[i], rhs.parameters[i])) {
      return false;
    }
  }
  return true;
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
  bool sawType = false;
  bool sawStatus = false;
  bool sawReflectionHash = false;
  bool sawStorageAbiHash = false;
  bool sawAccessorAbiHash = false;
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
      if (sawType) {
        result.diagnostics.push_back(sourceUri.string() + ": duplicate type");
      }
      sawType = true;
      reflection.declaredType = trim(std::string_view(stripped).substr(5));
    } else if (stripped.rfind("status:", 0) == 0) {
      if (sawStatus) {
        result.diagnostics.push_back(sourceUri.string() + ": duplicate status");
      }
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
      if (sawReflectionHash) {
        result.diagnostics.push_back(sourceUri.string() +
                                     ": duplicate reflection hash");
      }
      sawReflectionHash = true;
      reflection.reflectionHash = trim(std::string_view(stripped).substr(15));
    } else if (stripped.rfind("storageAbiHash:", 0) == 0) {
      if (sawStorageAbiHash) {
        result.diagnostics.push_back(sourceUri.string() +
                                     ": duplicate storage ABI hash");
      }
      sawStorageAbiHash = true;
      reflection.storageAbiHash = trim(std::string_view(stripped).substr(15));
    } else if (stripped.rfind("accessorAbiHash:", 0) == 0) {
      if (sawAccessorAbiHash) {
        result.diagnostics.push_back(sourceUri.string() +
                                     ": duplicate accessor ABI hash");
      }
      sawAccessorAbiHash = true;
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
      if (reflection.findParameter(parameter.name).has_value()) {
        result.diagnostics.push_back(sourceUri.string() + ": duplicate '" +
                                     parameter.name + "' parameter");
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

MaterialContractSourceLoadResult
loadMaterialContractSourceText(const LX_core::ResourceUri &sourceUri) {
  MaterialContractSourceLoadResult result;

  if (sourceUri.string().rfind("memory://", 0) == 0) {
    result.diagnostics.push_back(
        "unsupported material contract source URI scheme '" +
        sourceUri.string() +
        "'; inject a source loader for memory contract sources");
    return result;
  }

  if (hasUriScheme(sourceUri) &&
      sourceUri.string().rfind("assets://", 0) != 0) {
    result.diagnostics.push_back(
        "unsupported material contract source URI scheme '" +
        sourceUri.string() + "'");
    return result;
  }

  const std::filesystem::path path = filesystemPathForContractSource(sourceUri);
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    result.diagnostics.push_back("failed to read material contract source '" +
                                 sourceUri.string() + "' at '" +
                                 path.generic_string() + "'");
    return result;
  }

  std::ostringstream text;
  text << input.rdbuf();
  result.sourceText = text.str();
  return result;
}

MaterialContractReflectionResult loadAndReflectMaterialContractSource(
    const LX_core::ResourceUri &sourceUri,
    const MaterialContractReflector &reflector,
    const MaterialContractSourceLoader &sourceLoader) {
  MaterialContractReflectionResult result;
  const MaterialContractSourceLoadResult loaded = sourceLoader(sourceUri);
  result.diagnostics = loaded.diagnostics;
  if (!loaded.sourceText.has_value()) {
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(
          "material contract source loader returned no text");
    }
    return result;
  }

  MaterialContractReflectionResult reflected =
      reflector(sourceUri, *loaded.sourceText);
  result.reflection = std::move(reflected.reflection);
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(
                                reflected.diagnostics.begin()),
                            std::make_move_iterator(
                                reflected.diagnostics.end()));
  return result;
}

MaterialContractReflectionSetValidationResult
validateMaterialContractReflectionSet(
    const std::vector<LX_core::MaterialContractReflection> &reflections) {
  MaterialContractReflectionSetValidationResult result;

  for (std::size_t i = 0; i < reflections.size(); ++i) {
    for (std::size_t j = i + 1; j < reflections.size(); ++j) {
      if (reflections[i].sourceSignature() ==
              reflections[j].sourceSignature() &&
          !sameContractLayout(reflections[i], reflections[j])) {
        result.diagnostics.push_back(
            "material contract source signature conflict");
      }
    }
  }

  return result;
}

} // namespace LX_infra
