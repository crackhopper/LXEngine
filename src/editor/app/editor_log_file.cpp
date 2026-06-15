#include "editor/app/editor_log_file.hpp"

#include "core/utils/filesystem_tools.hpp"

#include <system_error>

namespace LX_demo::lxe_editor {

TeeStreambuf::TeeStreambuf(std::streambuf& primary, std::streambuf& secondary)
    : m_primary(primary), m_secondary(secondary) {}

int TeeStreambuf::overflow(const int ch) {
  if (ch == traits_type::eof()) {
    return traits_type::not_eof(ch);
  }

  const int primaryResult = m_primary.sputc(static_cast<char>(ch));
  const int secondaryResult = m_secondary.sputc(static_cast<char>(ch));
  if (primaryResult == traits_type::eof() ||
      secondaryResult == traits_type::eof()) {
    return traits_type::eof();
  }
  return ch;
}

int TeeStreambuf::sync() {
  const int primaryResult = m_primary.pubsync();
  const int secondaryResult = m_secondary.pubsync();
  return primaryResult == 0 && secondaryResult == 0 ? 0 : -1;
}

ScopedEditorLogFile::ScopedEditorLogFile(const std::filesystem::path& path)
    : m_file(), m_originalOut(*std::cout.rdbuf()),
      m_originalErr(*std::cerr.rdbuf()), m_outTee(m_originalOut, *m_file.rdbuf()),
      m_errTee(m_originalErr, *m_file.rdbuf()) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    std::cerr << "[lxe_editor] failed to create log directory "
              << path.parent_path() << ": " << ec.message() << "\n";
    return;
  }

  m_file.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!m_file.is_open()) {
    std::cerr << "[lxe_editor] failed to open editor log file " << path
              << "\n";
    return;
  }

  std::cout.rdbuf(&m_outTee);
  std::cerr.rdbuf(&m_errTee);
  m_enabled = true;
}

ScopedEditorLogFile::~ScopedEditorLogFile() {
  if (m_enabled) {
    std::cout.flush();
    std::cerr.flush();
    std::cout.rdbuf(&m_originalOut);
    std::cerr.rdbuf(&m_originalErr);
  }
}

bool ScopedEditorLogFile::isEnabled() const { return m_enabled; }

std::filesystem::path editorLogFilePath() {
  return resolveRuntimePath("data/lxe_editor") / "editor.log";
}

} // namespace LX_demo::lxe_editor
