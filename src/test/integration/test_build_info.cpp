#include "infra/build_info/build_info.hpp"

#include <iostream>
#include <string>

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

void testBuildInfoJsonAndStringUseSameSource() {
  const std::string version = LX_infra::currentBuildInfoString("test_binary");

  EXPECT(version.find("test_binary") != std::string::npos,
         "version string should include binary name");
  EXPECT(version.find("(") != std::string::npos,
         "version string should include build identity details");
  EXPECT(version.find(",") != std::string::npos,
         "version string should include build context");

  const std::string publicJson = LX_infra::currentBuildInfoJson("test_binary");
  EXPECT(publicJson.find("\"buildInfo\"") != std::string::npos,
         "public json should expose the composed build info string");
  EXPECT(publicJson.find("\"gitCommit\"") == std::string::npos,
         "public json should not expose individual git fields");
}

} // namespace

int main() {
  testBuildInfoJsonAndStringUseSameSource();
  if (failures != 0) {
    std::cerr << "test_build_info failed with " << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_build_info passed\n";
  return 0;
}
