#include "demos/lxe_editor/api_token_state.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace LX_demo::lxe_editor {
namespace {

constexpr std::string_view kTokenFileName = "api_token.txt";

[[nodiscard]] std::string trimTrailingWhitespace(std::string text) {
  while (!text.empty() &&
         (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
          text.back() == '\t')) {
    text.pop_back();
  }
  return text;
}

} // namespace

ApiTokenState::ApiTokenState(std::filesystem::path rootDir)
    : m_rootDir(std::move(rootDir)), m_tokenPath(m_rootDir / kTokenFileName) {}

const std::filesystem::path& ApiTokenState::rootDir() const {
  return m_rootDir;
}

const std::filesystem::path& ApiTokenState::tokenPath() const {
  return m_tokenPath;
}

std::string ApiTokenState::loadOrCreateToken() const {
  if (std::filesystem::exists(m_tokenPath)) {
    std::ifstream in(m_tokenPath, std::ios::in | std::ios::binary);
    std::string token((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    token = trimTrailingWhitespace(std::move(token));
    if (!token.empty()) {
      return token;
    }
  }

  const std::string token = generateToken();
  std::error_code ec;
  std::filesystem::create_directories(m_rootDir, ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to create api token directory "
              << m_rootDir << ": " << ec.message() << "\n";
    return {};
  }

  std::ofstream out(m_tokenPath, std::ios::out | std::ios::binary |
                                     std::ios::trunc);
  if (!out) {
    std::cerr << "[lxe_editor] failed to write api token file "
              << m_tokenPath << "\n";
    return {};
  }
  out << token;
  return token;
}

std::string ApiTokenState::generateToken() {
  std::random_device rd;
  std::mt19937_64 rng(rd());
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 32; ++i) {
    out << std::setw(2) << dist(rng);
  }
  return out.str();
}

} // namespace LX_demo::lxe_editor
