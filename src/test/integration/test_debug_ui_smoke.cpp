// REQ-018 smoke test for LX_infra::debug_ui.
//
// Two layers of verification:
//   1. Link-level: every public helper symbol resolves (captured into a
//      std::vector<void*> of function pointers).
//   2. CPU-only ImGui smoke: CreateContext → set minimal IO → NewFrame →
//      invoke a representative helper surface → EndFrame → DestroyContext.
//      No GPU / window / backend is required because we never call Render()
//      or touch a backend. If the environment refuses to create a context
//      (which should never happen for ImGui's in-memory CreateContext), we
//      skip cleanly so link-level verification still counts.

#include "infra/gui/debug_ui.hpp"

#include "core/scene/camera.hpp"
#include "core/scene/light.hpp"
#include "core/time/clock.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"

#include <imgui.h>

#include <iostream>
#include <string>
#include <vector>

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

// ---- Link-level symbol reachability --------------------------------------

void test_link_level_symbols_reachable() {
  namespace dui = LX_infra::debug_ui;

  // Capture typed function pointers for every public helper declared in
  // debug_ui.hpp. Missing or renamed symbols trip a compile error here;
  // cast to void* for a runtime null-check as the final belt-and-suspenders.
  bool (*pDragVec3)(const char*, LX_core::Vec3f&, float, float, float) =
      &dui::dragVec3;
  bool (*pDragVec4)(const char*, LX_core::Vec4f&, float, float, float) =
      &dui::dragVec4;
  bool (*pSliderFloat)(const char*, float&, float, float) = &dui::sliderFloat;
  bool (*pSliderInt)(const char*, int&, int, int) = &dui::sliderInt;
  bool (*pColorEdit3)(const char*, LX_core::Vec3f&) = &dui::colorEdit3;
  bool (*pColorEdit4)(const char*, LX_core::Vec4f&) = &dui::colorEdit4;

  void (*pLabelTextC)(const char*, const char*) =
      static_cast<void (*)(const char*, const char*)>(&dui::labelText);
  void (*pLabelTextS)(const char*, const std::string&) =
      static_cast<void (*)(const char*, const std::string&)>(&dui::labelText);
  void (*pLabelFloat)(const char*, float) = &dui::labelFloat;
  void (*pLabelInt)(const char*, int) = &dui::labelInt;
  void (*pLabelStringId)(const char*, LX_core::StringID) = &dui::labelStringId;

  bool (*pBeginPanel)(const char*) = &dui::beginPanel;
  void (*pEndPanel)() = &dui::endPanel;
  bool (*pBeginSection)(const char*) = &dui::beginSection;
  void (*pEndSection)() = &dui::endSection;
  void (*pSeparatorText)(const char*) = &dui::separatorText;

  void (*pRenderStatsPanel)(const LX_core::Clock&) = &dui::renderStatsPanel;
  void (*pCameraPanel)(const char*, LX_core::Camera&) = &dui::cameraPanel;
  void (*pDirectionalLightPanel)(const char*, LX_core::DirectionalLight&) =
      &dui::directionalLightPanel;

  const std::vector<void*> symbols = {
      reinterpret_cast<void*>(pDragVec3),
      reinterpret_cast<void*>(pDragVec4),
      reinterpret_cast<void*>(pSliderFloat),
      reinterpret_cast<void*>(pSliderInt),
      reinterpret_cast<void*>(pColorEdit3),
      reinterpret_cast<void*>(pColorEdit4),
      reinterpret_cast<void*>(pLabelTextC),
      reinterpret_cast<void*>(pLabelTextS),
      reinterpret_cast<void*>(pLabelFloat),
      reinterpret_cast<void*>(pLabelInt),
      reinterpret_cast<void*>(pLabelStringId),
      reinterpret_cast<void*>(pBeginPanel),
      reinterpret_cast<void*>(pEndPanel),
      reinterpret_cast<void*>(pBeginSection),
      reinterpret_cast<void*>(pEndSection),
      reinterpret_cast<void*>(pSeparatorText),
      reinterpret_cast<void*>(pRenderStatsPanel),
      reinterpret_cast<void*>(pCameraPanel),
      reinterpret_cast<void*>(pDirectionalLightPanel),
  };

  for (size_t i = 0; i < symbols.size(); ++i) {
    EXPECT(symbols[i] != nullptr,
           "debug_ui symbol at index " << i << " resolved to null");
  }
}

// ---- CPU-only ImGui smoke ------------------------------------------------

bool setupMinimalImGui() {
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280.0f, 720.0f);
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;

  // Build a font atlas so NewFrame doesn't assert. We don't upload the
  // texture anywhere — ImGui only requires the pixels to have been
  // retrieved once to satisfy its readiness check. Modern ImGui versions
  // pick up the atlas through io.Fonts->TexRef automatically once the pixel
  // data has been generated, so we only need to force the build here.
  unsigned char* pixels = nullptr;
  int w = 0;
  int h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  return pixels != nullptr && w > 0 && h > 0;
}

void test_cpu_only_imgui_smoke() {
  if (!setupMinimalImGui()) {
    std::cout
        << "[SKIP] cpu_only_imgui_smoke (font atlas unavailable in this env)\n";
    ImGui::DestroyContext();
    return;
  }

  namespace dui = LX_infra::debug_ui;

  LX_core::Vec3f v3{1.0f, 2.0f, 3.0f};
  LX_core::Vec4f color{0.1f, 0.2f, 0.3f, 1.0f};
  float scalar = 0.5f;
  LX_core::StringID sid("forward");
  LX_core::Clock clock;
  LX_core::Camera camera;
  LX_core::DirectionalLight light;

  try {
    ImGui::NewFrame();
    if (dui::beginPanel("SmokePanel")) {
      dui::separatorText("vectors");
      dui::dragVec3("v3", v3);
      dui::colorEdit4("col4", color);
      dui::sliderFloat("scalar", scalar, 0.0f, 1.0f);
      dui::labelText("label-c", "hello");
      dui::labelText("label-s", std::string("world"));
      dui::labelFloat("f", 1.5f);
      dui::labelInt("i", 42);
      dui::labelStringId("sid", sid);
      dui::labelStringId("empty", LX_core::StringID{});
      if (dui::beginSection("section")) {
        dui::labelText("inside", "yes");
      }
      dui::endSection();
      dui::renderStatsPanel(clock);
      dui::cameraPanel("camera", camera);
      dui::directionalLightPanel("sun", light);
    }
    dui::endPanel();
    ImGui::EndFrame();
  } catch (...) {
    EXPECT(false, "cpu_only_imgui_smoke: helper surface threw");
  }

  ImGui::DestroyContext();
}

} // namespace

int main() {
  expSetEnvVK();
  test_link_level_symbols_reachable();
  test_cpu_only_imgui_smoke();

  if (failures == 0) {
    std::cout << "[PASS] debug_ui smoke tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
