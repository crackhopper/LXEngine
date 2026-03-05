#include <iostream>
#include <filesystem>

void printCurrentDirectory_cpp17() {
  try {
    // std::filesystem::current_path() 返回当前工作目录的路径对象
    std::filesystem::path currentPath = std::filesystem::current_path();

    std::cout << "Current Working Directory (C++17): " << currentPath.string()
              << std::endl;

  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error getting current path: " << e.what() << std::endl;
  }
}