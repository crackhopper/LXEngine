#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

bool initializeRuntimeAssetRoot();
bool initializeRuntimeAssetRoot(const std::filesystem::path &hintPath);
std::filesystem::path getRuntimeAssetRoot();
std::filesystem::path resolveRuntimePath(
    const std::filesystem::path &relativePath);
std::filesystem::path getRuntimeShaderSourceDir();
std::filesystem::path getRuntimeShaderBinaryDir();
bool cdToWhereShadersExist(const std::string& shaderName);
bool cdToWhereResourcesCouldFound(const std::string& shaderName);
bool cdToWhereAssetsExist(const std::string& subpath);
std::string getShaderPath(const std::string& shaderName,
                          const std::string &stageSuffix);
std::vector<char> readFile(const std::string &filepath);
