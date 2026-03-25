#include "env.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#endif

void expSetEnvVK() {
#ifdef _WIN32
  // SetEnvironmentVariableW(
  //     L"VK_DRIVER_FILES",
  //     LR"(C:\WINDOWS\System32\DriverStore\FileRepository\nvmi.inf_amd64_c6ae241e95feb82d\nv-vk64.json)");

  SetEnvironmentVariableW(L"VK_LOADER_LAYERS_DISABLE", L"~implicit~");
#else
  // setenv(
  //     "VK_DRIVER_FILES",
  //     R"(C:\WINDOWS\System32\DriverStore\FileRepository\nvmi.inf_amd64_c6ae241e95feb82d\nv-vk64.json)",
  //     1);
  setenv("VK_LOADER_LAYERS_DISABLE", "~implicit~", 1);
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