#pragma once

#include "demos/lxe_editor/lxe_editor_api_service.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace LX_demo::lxe_editor {

struct LxeEditorApiServerConfig final {
  bool enabled = true;
  std::string host = "0.0.0.0";
  std::uint16_t port = 3768;
  std::string token;
};

class LxeEditorApiServer final {
public:
  explicit LxeEditorApiServer(LxeEditorApiServerConfig config = {});
  ~LxeEditorApiServer();

  LxeEditorApiServer(const LxeEditorApiServer&) = delete;
  LxeEditorApiServer& operator=(const LxeEditorApiServer&) = delete;

  [[nodiscard]] bool start(std::string* errorMessage = nullptr);
  void stop();
  void pump(LxeEditorApiService& service);

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] const LxeEditorApiServerConfig& config() const;
  [[nodiscard]] std::uint16_t boundPort() const;

private:
  struct Impl;

  LxeEditorApiServerConfig m_config;
  Impl* m_impl = nullptr;
};

} // namespace LX_demo::lxe_editor
