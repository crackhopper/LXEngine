#include "editor/app/editor_log_file.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace demo = LX_demo::lxe_editor;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] std::filesystem::path makeTempPath(const char* filename) {
  return std::filesystem::temp_directory_path() / filename;
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void testEditorLogFileTruncatesAndCapturesStdoutAndStderr() {
  const std::filesystem::path path = makeTempPath("lxe_editor_log_file.log");
  {
    std::ofstream stale(path, std::ios::out | std::ios::binary |
                                  std::ios::trunc);
    stale << "stale log\n";
  }

  std::ostringstream visibleOut;
  std::ostringstream visibleErr;
  auto& originalOut = *std::cout.rdbuf(visibleOut.rdbuf());
  auto& originalErr = *std::cerr.rdbuf(visibleErr.rdbuf());
  {
    demo::ScopedEditorLogFile logFile(path);
    std::cout << "stdout line\n";
    std::cerr << "stderr line\n";
  }
  std::cout.rdbuf(&originalOut);
  std::cerr.rdbuf(&originalErr);

  const std::string logText = readFile(path);
  EXPECT(logText.find("stale log") == std::string::npos,
         "new log file should truncate old contents");
  EXPECT(logText.find("stdout line\n") != std::string::npos,
         "log file should capture stdout");
  EXPECT(logText.find("stderr line\n") != std::string::npos,
         "log file should capture stderr");
  EXPECT(visibleOut.str() == "stdout line\n",
         "stdout should still reach its original stream");
  EXPECT(visibleErr.str() == "stderr line\n",
         "stderr should still reach its original stream");
}

} // namespace

int main() {
  testEditorLogFileTruncatesAndCapturesStdoutAndStderr();

  if (failures != 0) {
    std::cerr << "test_lxe_editor_log_file failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_lxe_editor_log_file passed\n";
  return 0;
}
