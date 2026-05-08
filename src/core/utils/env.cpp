#include "env.hpp"
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#endif

namespace {
#ifdef _WIN32
void setLoaderEnv(const wchar_t *name, const wchar_t *value) {
  SetEnvironmentVariableW(name, value);
}

void clearLoaderEnv(const wchar_t *name) { SetEnvironmentVariableW(name, nullptr); }
#else
void setLoaderEnv(const char *name, const char *value) { setenv(name, value, 1); }

void clearLoaderEnv(const char *name) { unsetenv(name); }
#endif
} // namespace

bool expEnvEnabled(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr && std::strcmp(value, "0") != 0;
}

bool expRendererDebugEnabled() { return expEnvEnabled("LX_RENDER_DEBUG"); }

bool expSceneViewerDebugEnabled() {
  return expEnvEnabled("LX_SCENE_VIEWER_DEBUG");
}

void expSetEnvVK() {
  if (expEnvEnabled("LX_ALLOW_IMPLICIT_VK_LAYERS")) {
    clearLoaderEnv(
#ifdef _WIN32
        L"VK_LOADER_LAYERS_DISABLE"
#else
        "VK_LOADER_LAYERS_DISABLE"
#endif
    );
    clearLoaderEnv(
#ifdef _WIN32
        L"VK_LOADER_LAYERS_ALLOW"
#else
        "VK_LOADER_LAYERS_ALLOW"
#endif
    );
    return;
  }

  // Short alias: LX_RENDERDOC=1 implies LX_ALLOW_RENDERDOC_IMPLICIT_LAYER=1.
  // Either env var unblocks the RenderDoc capture implicit layer.
  const bool allowRenderDocImplicitLayer =
      expEnvEnabled("LX_ALLOW_RENDERDOC_IMPLICIT_LAYER") ||
      expEnvEnabled("LX_RENDERDOC");

#ifdef _WIN32
  // SetEnvironmentVariableW(
  //     L"VK_DRIVER_FILES",
  //     LR"(C:\WINDOWS\System32\DriverStore\FileRepository\nvmi.inf_amd64_c6ae241e95feb82d\nv-vk64.json)");

  setLoaderEnv(L"VK_LOADER_LAYERS_DISABLE", L"~implicit~");
  if (allowRenderDocImplicitLayer) {
    setLoaderEnv(L"VK_LOADER_LAYERS_ALLOW", L"VK_LAYER_RENDERDOC_Capture");
  } else {
    clearLoaderEnv(L"VK_LOADER_LAYERS_ALLOW");
  }
#else
  // setenv(
  //     "VK_DRIVER_FILES",
  //     R"(C:\WINDOWS\System32\DriverStore\FileRepository\nvmi.inf_amd64_c6ae241e95feb82d\nv-vk64.json)",
  //     1);
  setLoaderEnv("VK_LOADER_LAYERS_DISABLE", "~implicit~");
  if (allowRenderDocImplicitLayer) {
    setLoaderEnv("VK_LOADER_LAYERS_ALLOW", "VK_LAYER_RENDERDOC_Capture");
  } else {
    clearLoaderEnv("VK_LOADER_LAYERS_ALLOW");
  }
#endif
}

// // 告诉 NVIDIA 驱动程序使用独立显卡 (dGPU)
// extern "C" {
//   __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
// }

// // 告诉 AMD 驱动程序使用独立显卡 (dGPU) (可选)
// extern "C" {
//   __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
// }
