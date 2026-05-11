#pragma once

#include "demos/lxe_editor/editor_automation_service.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace LX_demo::lxe_editor {

struct EditorAutomationServerConfig final {
  bool enabled = true;
  std::string host = "0.0.0.0";
  std::uint16_t port = 3768;
  std::string token;
};

class EditorAutomationServer final {
public:
  explicit EditorAutomationServer(EditorAutomationServerConfig config = {});
  ~EditorAutomationServer();

  EditorAutomationServer(const EditorAutomationServer&) = delete;
  EditorAutomationServer& operator=(const EditorAutomationServer&) = delete;

  [[nodiscard]] bool start(std::string* errorMessage = nullptr);
  void stop();
  void pump(EditorAutomationService& service);

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] const EditorAutomationServerConfig& config() const;
  [[nodiscard]] std::uint16_t boundPort() const;

private:
  struct Impl;

  EditorAutomationServerConfig m_config;
  Impl* m_impl = nullptr;
};

} // namespace LX_demo::lxe_editor
