#include "demos/lxe_editor/recording_controller.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

[[nodiscard]] std::filesystem::path makeTempRoot(const char* name) {
  const auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void testDisabledRecorderHasNoSideEffects() {
  const auto root = makeTempRoot("lxe_recording_disabled");
  demo::RecordingController recorder(root);

  const auto appendResult = recorder.appendStep(demo::RecordingStepInput{
      .kind = "command",
      .source = demo::RecordingSource::Mcp,
      .payloadJson = "{\"line\":\"help\"}",
  });

  EXPECT(!appendResult.recorded, "disabled recorder should not record steps");
  EXPECT(recorder.status().stepCount == 0, "disabled recorder should keep zero steps");
  EXPECT(!std::filesystem::exists(root / "recordings"),
         "disabled append should not create recording directories");
}

void testStartAppendStopAndSaveRecording() {
  const auto root = makeTempRoot("lxe_recording_save");
  demo::RecordingController recorder(root);

  recorder.enable();
  const auto start = recorder.start(demo::RecordingStartOptions{
      .detailLevel = demo::RecordingDetailLevel::Basic,
      .scenePath = "assets/scenes/lxe_editor.scene.yaml",
      .windowWidth = 1600,
      .windowHeight = 900,
  });
  EXPECT(start.active, "recording should be active after start");
  EXPECT(recorder.status().enabled, "recorder should stay enabled");

  const auto appendResult = recorder.appendStep(demo::RecordingStepInput{
      .kind = "command",
      .source = demo::RecordingSource::Mcp,
      .payloadJson = "{\"line\":\"scene load lxe_editor.scene.yaml\"}",
  });
  EXPECT(appendResult.recorded, "active recorder should append command step");
  EXPECT(appendResult.stepId == 1, "first recorded step id should be 1");

  const auto stop = recorder.stop(demo::RecordingStopOptions{.save = true});
  EXPECT(!recorder.status().active, "recording should be inactive after stop");
  EXPECT(stop.saved, "stop(save=true) should write a file");
  EXPECT(std::filesystem::exists(stop.path), "saved recording path should exist");

  const std::string text = readFile(stop.path);
  EXPECT(text.find("\"schemaVersion\":1") != std::string::npos,
         "recording should include schema version");
  EXPECT(text.find("\"detailLevel\":\"basic\"") != std::string::npos,
         "recording should include detail level");
  EXPECT(text.find("\"source\":\"mcp\"") != std::string::npos,
         "recording should include mcp source");
  EXPECT(text.find("scene load lxe_editor.scene.yaml") != std::string::npos,
         "recording should include command payload");

  const auto entries = recorder.list();
  EXPECT(entries.size() == 1, "saved recording should be listed");
  if (!entries.empty()) {
    const std::string byId = recorder.read(entries.front().id);
    EXPECT(byId.find("scene load lxe_editor.scene.yaml") != std::string::npos,
           "recording list id should be readable");
  }
}

} // namespace

int main() {
  testDisabledRecorderHasNoSideEffects();
  testStartAppendStopAndSaveRecording();

  if (failures != 0) {
    std::cerr << "[FAIL] lxe_editor recording tests: " << failures << "\n";
    return 1;
  }
  std::cout << "[PASS] lxe_editor recording tests passed.\n";
  return 0;
}
