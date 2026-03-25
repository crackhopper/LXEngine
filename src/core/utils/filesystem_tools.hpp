#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

bool cdToWhereShadersExist(const std::string& shaderName);
std::string getShaderPath(const std::string& shaderName);
std::vector<char> readFile(const std::string &filepath);