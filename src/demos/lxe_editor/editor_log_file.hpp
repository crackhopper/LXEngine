#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <streambuf>

namespace LX_demo::lxe_editor {

class TeeStreambuf final : public std::streambuf {
public:
  TeeStreambuf(std::streambuf& primary, std::streambuf& secondary);

protected:
  int overflow(int ch) override;
  int sync() override;

private:
  std::streambuf& m_primary;
  std::streambuf& m_secondary;
};

class ScopedEditorLogFile final {
public:
  explicit ScopedEditorLogFile(const std::filesystem::path& path);
  ScopedEditorLogFile(const ScopedEditorLogFile&) = delete;
  ScopedEditorLogFile(ScopedEditorLogFile&&) noexcept = delete;
  ScopedEditorLogFile& operator=(const ScopedEditorLogFile&) = delete;
  ScopedEditorLogFile& operator=(ScopedEditorLogFile&&) noexcept = delete;
  ~ScopedEditorLogFile();

  [[nodiscard]] bool isEnabled() const;

private:
  std::ofstream m_file;
  std::streambuf& m_originalOut;
  std::streambuf& m_originalErr;
  TeeStreambuf m_outTee;
  TeeStreambuf m_errTee;
  bool m_enabled = false;
};

[[nodiscard]] std::filesystem::path editorLogFilePath();

} // namespace LX_demo::lxe_editor
