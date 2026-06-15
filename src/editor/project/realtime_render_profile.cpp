#include "editor/project/realtime_render_profile.hpp"

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

[[nodiscard]] char hexDigit(unsigned int value) {
  return static_cast<char>(value < 10u ? ('0' + value) : ('A' + value - 10u));
}

[[nodiscard]] std::string encodePathComponent(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char c : text) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (safe) {
      out.push_back(static_cast<char>(c));
      continue;
    }
    out.push_back('%');
    out.push_back(hexDigit((c >> 4u) & 0x0fu));
    out.push_back(hexDigit(c & 0x0fu));
  }
  return out.empty() ? "unnamed" : out;
}

[[nodiscard]] std::string pathJson(const std::filesystem::path &path) {
  return jsonEscape(path.generic_string());
}

} // namespace

std::filesystem::path makeRealtimeProfileOutputBasePath(
    std::string_view sceneName, std::string_view profileName,
    const LX_core::offline::OutputProfile &output) {
  return output.outDir / "realtime" / encodePathComponent(sceneName) /
         encodePathComponent(profileName) / "render";
}

std::string realtimeProfileOutputResultJson(
    std::string_view profileName, const RealtimeProfileOutputResult &result) {
  std::ostringstream oss;
  oss << "{\"profile\":\"" << jsonEscape(profileName) << "\",\"width\":"
      << result.width << ",\"height\":" << result.height
      << ",\"linearExrPath\":\"" << pathJson(result.linearExrPath)
      << "\",\"cpuSrgbPngPath\":\"" << pathJson(result.cpuSrgbPngPath)
      << "\",\"pipelineSrgbPngPath\":\""
      << pathJson(result.pipelineSrgbPngPath) << "\",\"metadataPath\":\""
      << pathJson(result.metadataPath) << "\"}";
  return oss.str();
}

} // namespace LX_demo::lxe_editor
