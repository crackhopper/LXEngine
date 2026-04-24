#include "core/utils/env.hpp"
#include <iostream>

int main() {
  expSetEnvVK();
  std::cout
      << "Renderer is a bootstrap/env-probe executable.\n"
      << "Use demo_scene_viewer for the default interactive demo.\n";
  return 0;
}
