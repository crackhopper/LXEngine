#include "editor/project/debug_render_export.hpp"

#include <sstream>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string pathJson(const std::filesystem::path &path) {
  return jsonEscape(path.generic_string());
}

} // namespace

std::string debugColorTransferExportResultJson(
    const LX_core::backend::VulkanDebugColorTransferExportResult &result) {
  std::ostringstream oss;
  oss << "{\"manifestPath\":\"" << pathJson(result.manifestPath)
      << "\",\"outputDirectory\":\"" << pathJson(result.outputDirectory)
      << "\",\"targets\":[";
  for (usize i = 0; i < result.targets.size(); ++i) {
    const auto &target = result.targets[i];
    if (i != 0) {
      oss << ',';
    }
    oss << "{\"name\":\"" << jsonEscape(target.name) << "\",\"path\":\""
        << pathJson(target.path) << "\",\"format\":\""
        << jsonEscape(target.format) << "\",\"width\":" << target.width
        << ",\"height\":" << target.height << "}";
  }
  oss << "]}";
  return oss.str();
}

} // namespace LX_demo::lxe_editor
