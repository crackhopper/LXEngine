#include "editor/project/debug_render_export.hpp"

#include <iomanip>
#include <sstream>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  std::ostringstream escapedControl;
  for (const unsigned char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    default:
      if (c < 0x20u) {
        escapedControl.str({});
        escapedControl.clear();
        escapedControl << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(c);
        out += escapedControl.str();
      } else {
        out.push_back(static_cast<char>(c));
      }
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
