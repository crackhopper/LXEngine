#pragma once

#include "demos/lxe_editor/editor_automation_service.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace LX_demo::lxe_editor {

struct EditorMcpServerConfig final {
  bool enabled = true;
  std::string host = "127.0.0.1";
  std::uint16_t port = 3769;
};

class EditorMcpServer final {
public:
  explicit EditorMcpServer(EditorMcpServerConfig config = {});
  ~EditorMcpServer();

  EditorMcpServer(const EditorMcpServer&) = delete;
  EditorMcpServer& operator=(const EditorMcpServer&) = delete;

  [[nodiscard]] bool start(std::string* errorMessage = nullptr);
  void stop();
  void pump(EditorAutomationService& service);

  [[nodiscard]] bool isRunning() const;
  [[nodiscard]] const EditorMcpServerConfig& config() const;
  [[nodiscard]] std::uint16_t boundPort() const;

private:
  struct Impl;

  EditorMcpServerConfig m_config;
  std::unique_ptr<Impl> m_impl;
};

} // namespace LX_demo::lxe_editor
