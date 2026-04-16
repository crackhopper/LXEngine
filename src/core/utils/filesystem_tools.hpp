#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

bool cdToWhereShadersExist(const std::string& shaderName);
bool cdToWhereResourcesCouldFound(const std::string& shaderName);
bool cdToWhereAssetsExist(const std::string& subpath);
std::string getShaderPath(const std::string& shaderName);
std::vector<char> readFile(const std::string &filepath);
