#include "demos/lxe_editor/editor_mcp_server.hpp"

#include "demos/lxe_editor/editor_mcp_protocol.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace LX_demo::lxe_editor {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

constexpr std::string_view kJsonRpcVersion = "2.0";
constexpr std::string_view kProtocolVersion = "2025-03-26";

void closeSocket(const SocketHandle socket) {
  if (socket == kInvalidSocket) {
    return;
  }
#if defined(_WIN32)
  closesocket(socket);
#else
  close(socket);
#endif
}

[[nodiscard]] bool setNonBlocking(const SocketHandle socket) {
#if defined(_WIN32)
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

[[nodiscard]] bool wouldBlock() {
#if defined(_WIN32)
  const int error = WSAGetLastError();
  return error == WSAEWOULDBLOCK;
#else
  return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

[[nodiscard]] std::string socketErrorString() {
#if defined(_WIN32)
  return std::to_string(WSAGetLastError());
#else
  return std::strerror(errno);
#endif
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  return automationJsonEscape(text);
}

[[nodiscard]] std::string jsonQuoted(std::string_view text) {
  return std::string("\"") + jsonEscape(text) + "\"";
}

[[nodiscard]] std::string buildSummaryJson(const AutomationStateSnapshot& state) {
  std::ostringstream out;
  out << "{\"sceneName\":\"" << jsonEscape(state.scene.sceneName) << "\""
      << ",\"currentDocumentPath\":\""
      << jsonEscape(state.scene.currentDocumentPath) << "\""
      << ",\"sourceKind\":\""
      << automationSceneSourceKindName(state.scene.sourceKind) << "\""
      << ",\"permission\":\""
      << automationPermissionLevelName(state.scene.permission) << "\""
      << ",\"dirty\":" << (state.scene.dirty ? "true" : "false")
      << ",\"previewEnabled\":"
      << (state.toolbar.previewEnabled ? "true" : "false")
      << ",\"mode\":\"" << automationEditModeName(state.toolbar.editMode) << "\""
      << ",\"selectionCount\":" << state.selection.selectedPaths.size()
      << ",\"activeCameraPath\":\""
      << jsonEscape(state.cameras.activeCameraPath) << "\"}";
  return out.str();
}

[[nodiscard]] std::string resourceJson(const std::string_view uri,
                                       const AutomationStateSnapshot& state) {
  if (uri == "lxe-editor://summary") {
    return buildSummaryJson(state);
  }
  if (uri == "lxe-editor://selection") {
    return toJson(state.selection);
  }
  if (uri == "lxe-editor://cameras") {
    return toJson(state.cameras);
  }
  if (uri == "lxe-editor://toolbar") {
    return toJson(state.toolbar);
  }
  if (uri == "lxe-editor://scene") {
    return toJson(state.scene);
  }
  return "{}";
}

[[nodiscard]] std::string makeHeaderFrame(const std::string& payload) {
  std::ostringstream out;
  out << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
  return out.str();
}

[[nodiscard]] std::string makeResultEnvelope(
    const std::string& idJson, const std::string& resultJson) {
  return std::string("{\"jsonrpc\":\"") + std::string(kJsonRpcVersion) +
         "\",\"id\":" + idJson + ",\"result\":" + resultJson + "}";
}

[[nodiscard]] std::string makeErrorEnvelope(const std::string& idJson,
                                            const int code,
                                            std::string_view message) {
  return std::string("{\"jsonrpc\":\"") + std::string(kJsonRpcVersion) +
         "\",\"id\":" + idJson + ",\"error\":{\"code\":" +
         std::to_string(code) + ",\"message\":\"" + jsonEscape(message) +
         "\"}}";
}

[[nodiscard]] std::string toolCallResult(std::string_view text,
                                         std::string structuredJson = {},
                                         const bool isError = false) {
  std::ostringstream out;
  out << "{\"content\":[{\"type\":\"text\",\"text\":\"" << jsonEscape(text)
      << "\"}]";
  if (!structuredJson.empty()) {
    out << ",\"structuredContent\":" << structuredJson;
  }
  if (isError) {
    out << ",\"isError\":true";
  }
  out << "}";
  return out.str();
}

[[nodiscard]] std::string initializeResult(const std::string& protocolVersion) {
  return std::string("{\"protocolVersion\":\"") + protocolVersion +
         "\",\"capabilities\":{\"tools\":{\"listChanged\":false},"
         "\"resources\":{\"subscribe\":false,\"listChanged\":false},"
         "\"prompts\":{\"listChanged\":false}},"
         "\"serverInfo\":{\"name\":\"lxe_editor\",\"version\":\"1.0\"}}";
}

[[nodiscard]] std::string toolsListResult() {
  return std::string(
      "{\"tools\":["
      "{\"name\":\"lxe_editor_command\",\"description\":\"Execute a "
      "command console line.\",\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"line\":{\"type\":\"string\"}},\"required\":[\"line\"]}},"
      "{\"name\":\"lxe_editor_get_summary\",\"description\":\"Read the current "
      "editor summary.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
      "{\"name\":\"lxe_editor_get_selection\",\"description\":\"Read the current "
      "selection state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
      "{\"name\":\"lxe_editor_get_cameras\",\"description\":\"Read editor, "
      "game, and active camera state.\",\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{}}},"
      "{\"name\":\"lxe_editor_pick\",\"description\":\"Run a screen-space pick "
      "through the command surface.\",\"inputSchema\":{\"type\":\"object\","
      "\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},"
      "\"required\":[\"x\",\"y\"]}},"
      "{\"name\":\"lxe_editor_wait_for\",\"description\":\"Check whether a "
      "resource text currently contains an expected substring.\","
      "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
      "\"resource\":{\"type\":\"string\"},"
      "\"contains\":{\"type\":\"string\"}}}},"
      "{\"name\":\"lxe_editor_ensure_running\",\"description\":\"Return the "
      "current summary if the editor MCP server is reachable.\","
      "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
      "]}");
}

[[nodiscard]] std::string resourcesListResult() {
  return std::string(
      "{\"resources\":["
      "{\"uri\":\"lxe-editor://summary\",\"name\":\"summary\","
      "\"description\":\"High-level editor summary.\",\"mimeType\":\"application/json\"},"
      "{\"uri\":\"lxe-editor://selection\",\"name\":\"selection\","
      "\"description\":\"Current selection state.\",\"mimeType\":\"application/json\"},"
      "{\"uri\":\"lxe-editor://cameras\",\"name\":\"cameras\","
      "\"description\":\"Current camera state.\",\"mimeType\":\"application/json\"},"
      "{\"uri\":\"lxe-editor://toolbar\",\"name\":\"toolbar\","
      "\"description\":\"Current toolbar mode and preview state.\",\"mimeType\":\"application/json\"},"
      "{\"uri\":\"lxe-editor://scene\",\"name\":\"scene\","
      "\"description\":\"Current scene summary.\",\"mimeType\":\"application/json\"}"
      "]}");
}

[[nodiscard]] std::string resourceReadResult(std::string_view uri,
                                             const std::string& text) {
  return std::string("{\"contents\":[{\"uri\":\"") + jsonEscape(uri) +
         "\",\"mimeType\":\"application/json\",\"text\":\"" +
         jsonEscape(text) + "\"}]}";
}

[[nodiscard]] std::optional<std::string> scalarString(const YAML::Node& node,
                                                      const char* key) {
  if (!node[key] || !node[key].IsScalar()) {
    return std::nullopt;
  }
  return node[key].as<std::string>();
}

[[nodiscard]] std::optional<double> scalarNumber(const YAML::Node& node,
                                                 const char* key) {
  if (!node[key] || !node[key].IsScalar()) {
    return std::nullopt;
  }
  try {
    return node[key].as<double>();
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<LxeEditorMcpMessage>
parseMessage(const std::string& payload, std::string& error) {
  try {
    const YAML::Node root = YAML::Load(payload);
    if (!root || !root["method"]) {
      error = "missing method";
      return std::nullopt;
    }

    LxeEditorMcpMessage message;
    message.method = root["method"].as<std::string>();

    if (root["id"]) {
      if (!root["id"].IsScalar()) {
        error = "unsupported non-scalar id";
        return std::nullopt;
      }
      try {
        const std::string asString = root["id"].as<std::string>();
        if (root["id"].Tag() == "!" || !asString.empty() &&
                                       !std::isdigit(asString.front())) {
          message.idJson = jsonQuoted(asString);
        } else {
          message.idJson = asString;
        }
      } catch (...) {
        error = "invalid id";
        return std::nullopt;
      }
    }

    if (root["params"]) {
      YAML::Emitter out;
      out << YAML::Flow << root["params"];
      message.paramsJson = out.c_str();
    }
    return message;
  } catch (const std::exception& e) {
    error = e.what();
    return std::nullopt;
  }
}

} // namespace

struct EditorMcpServer::Impl final {
  explicit Impl(EditorMcpServerConfig cfg) : config(std::move(cfg)) {}

  EditorMcpServerConfig config;
  SocketHandle listener = kInvalidSocket;
  SocketHandle client = kInvalidSocket;
  std::uint16_t boundPort = 0;
  bool running = false;
  std::thread thread;
  std::mutex mutex;
  std::vector<std::string> pendingPayloads;
  std::deque<std::string> outgoingFrames;
  std::string incomingBuffer;

  ~Impl() {
    closeSocket(client);
    closeSocket(listener);
  }

  void ioLoop() {
    while (running) {
      if (client == kInvalidSocket) {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);
        const SocketHandle accepted =
            accept(listener, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        if (accepted != kInvalidSocket) {
          if (setNonBlocking(accepted)) {
            client = accepted;
            incomingBuffer.clear();
          } else {
            closeSocket(accepted);
          }
        }
      }

      if (client != kInvalidSocket) {
        char buffer[4096];
        const int received =
            recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received > 0) {
          incomingBuffer.append(buffer, buffer + received);
          drainMessages();
        } else if (received == 0 || (received < 0 && !wouldBlock())) {
          closeSocket(client);
          client = kInvalidSocket;
          incomingBuffer.clear();
        }

        while (client != kInvalidSocket) {
          std::lock_guard<std::mutex> lock(mutex);
          if (outgoingFrames.empty()) {
            break;
          }

          std::string& frame = outgoingFrames.front();
          const int sent =
              send(client, frame.data(), static_cast<int>(frame.size()), 0);
          if (sent > 0) {
            const size_t sentBytes = static_cast<size_t>(sent);
            if (sentBytes >= frame.size()) {
              outgoingFrames.pop_front();
              continue;
            }
            frame.erase(0, sentBytes);
            break;
          }
          if (sent < 0 && wouldBlock()) {
            break;
          }
          closeSocket(client);
          client = kInvalidSocket;
          incomingBuffer.clear();
          break;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  void drainMessages() {
    while (true) {
      const size_t headerEnd = incomingBuffer.find("\r\n\r\n");
      if (headerEnd == std::string::npos) {
        return;
      }

      std::istringstream headerStream(incomingBuffer.substr(0, headerEnd));
      std::string line;
      size_t contentLength = 0;
      while (std::getline(headerStream, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        constexpr std::string_view prefix = "Content-Length:";
        if (line.rfind(prefix.data(), 0) == 0) {
          contentLength = static_cast<size_t>(
              std::stoul(std::string(line.substr(prefix.size()))));
        }
      }

      const size_t payloadOffset = headerEnd + 4;
      if (incomingBuffer.size() < payloadOffset + contentLength) {
        return;
      }

      std::string payload =
          incomingBuffer.substr(payloadOffset, contentLength);
      incomingBuffer.erase(0, payloadOffset + contentLength);

      std::lock_guard<std::mutex> lock(mutex);
      pendingPayloads.push_back(std::move(payload));
    }
  }
};

EditorMcpServer::EditorMcpServer(EditorMcpServerConfig config)
    : m_config(std::move(config)),
      m_impl(std::make_unique<Impl>(m_config)) {}

EditorMcpServer::~EditorMcpServer() { stop(); }

bool EditorMcpServer::start(std::string* errorMessage) {
  if (!m_config.enabled) {
    return true;
  }
  if (m_impl->running) {
    return true;
  }

  const SocketHandle listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener == kInvalidSocket) {
    if (errorMessage != nullptr) {
      *errorMessage = "failed to create MCP socket: " + socketErrorString();
    }
    return false;
  }

  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(m_config.port);
  if (inet_pton(AF_INET, m_config.host.c_str(), &addr.sin_addr) != 1) {
    closeSocket(listener);
    if (errorMessage != nullptr) {
      *errorMessage = "invalid MCP host: " + m_config.host;
    }
    return false;
  }

  if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closeSocket(listener);
    if (errorMessage != nullptr) {
      *errorMessage = "failed to bind MCP socket: " + socketErrorString();
    }
    return false;
  }
  if (listen(listener, 4) != 0) {
    closeSocket(listener);
    if (errorMessage != nullptr) {
      *errorMessage = "failed to listen on MCP socket: " + socketErrorString();
    }
    return false;
  }
  if (!setNonBlocking(listener)) {
    closeSocket(listener);
    if (errorMessage != nullptr) {
      *errorMessage = "failed to make MCP listener non-blocking";
    }
    return false;
  }

  sockaddr_in boundAddr{};
  socklen_t len = sizeof(boundAddr);
  if (getsockname(listener, reinterpret_cast<sockaddr*>(&boundAddr), &len) ==
      0) {
    m_impl->boundPort = ntohs(boundAddr.sin_port);
  } else {
    m_impl->boundPort = m_config.port;
  }

  m_impl->listener = listener;
  m_impl->running = true;
  m_impl->thread = std::thread([this] { m_impl->ioLoop(); });
  return true;
}

void EditorMcpServer::stop() {
  if (!m_impl->running) {
    return;
  }
  m_impl->running = false;
  if (m_impl->thread.joinable()) {
    m_impl->thread.join();
  }
  closeSocket(m_impl->client);
  closeSocket(m_impl->listener);
  m_impl->client = kInvalidSocket;
  m_impl->listener = kInvalidSocket;
  m_impl->boundPort = 0;
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->pendingPayloads.clear();
  m_impl->outgoingFrames.clear();
  m_impl->incomingBuffer.clear();
}

void EditorMcpServer::pump(EditorAutomationService& service) {
  if (!m_impl->running) {
    return;
  }

  std::vector<std::string> payloads;
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    payloads.swap(m_impl->pendingPayloads);
  }

  if (payloads.empty()) {
    return;
  }

  std::vector<std::string> frames;
  frames.reserve(payloads.size());
  const auto captureFreshState = [&service]() {
    return service.captureState();
  };

  for (const std::string& payload : payloads) {
    std::string parseError;
    const auto message = parseMessage(payload, parseError);
    if (!message.has_value()) {
      frames.push_back(makeHeaderFrame(makeErrorEnvelope(
          "null", -32700, "parse error: " + parseError)));
      continue;
    }

    if (!message->idJson.has_value() &&
        message->method == "notifications/initialized") {
      continue;
    }

    const std::string idJson = message->idJson.value_or("null");

    if (message->method == "initialize") {
      frames.push_back(
          makeHeaderFrame(makeResultEnvelope(
              idJson, initializeResult(std::string(kProtocolVersion)))));
      continue;
    }

    if (message->method == "ping") {
      frames.push_back(makeHeaderFrame(makeResultEnvelope(idJson, "{}")));
      continue;
    }
    if (message->method == "tools/list") {
      frames.push_back(
          makeHeaderFrame(makeResultEnvelope(idJson, toolsListResult())));
      continue;
    }
    if (message->method == "resources/list") {
      frames.push_back(
          makeHeaderFrame(makeResultEnvelope(idJson, resourcesListResult())));
      continue;
    }
    if (message->method == "prompts/list") {
      frames.push_back(
          makeHeaderFrame(makeResultEnvelope(idJson, "{\"prompts\":[]}")));
      continue;
    }
    if (message->method == "resources/read") {
      const AutomationStateSnapshot state = captureFreshState();
      YAML::Node params =
          message->paramsJson.empty() ? YAML::Node{} : YAML::Load(message->paramsJson);
      const std::string uri =
          scalarString(params, "uri").value_or("lxe-editor://summary");
      const std::string text = resourceJson(uri, state);
      frames.push_back(
          makeHeaderFrame(makeResultEnvelope(idJson, resourceReadResult(uri, text))));
      continue;
    }
    if (message->method == "tools/call") {
      YAML::Node params =
          message->paramsJson.empty() ? YAML::Node{} : YAML::Load(message->paramsJson);
      const std::optional<std::string> name = scalarString(params, "name");
      const YAML::Node arguments = params["arguments"];
      if (!name.has_value()) {
        frames.push_back(makeHeaderFrame(
            makeErrorEnvelope(idJson, -32602, "missing tool name")));
        continue;
      }

      if (*name == "lxe_editor_get_summary" || *name == "lxe_editor_ensure_running") {
        const AutomationStateSnapshot state = captureFreshState();
        const std::string text = buildSummaryJson(state);
        frames.push_back(makeHeaderFrame(
            makeResultEnvelope(idJson, toolCallResult(text, text))));
        continue;
      }
      if (*name == "lxe_editor_get_selection") {
        const AutomationStateSnapshot state = captureFreshState();
        const std::string text = toJson(state.selection);
        frames.push_back(makeHeaderFrame(
            makeResultEnvelope(idJson, toolCallResult(text, text))));
        continue;
      }
      if (*name == "lxe_editor_get_cameras") {
        const AutomationStateSnapshot state = captureFreshState();
        const std::string text = toJson(state.cameras);
        frames.push_back(makeHeaderFrame(
            makeResultEnvelope(idJson, toolCallResult(text, text))));
        continue;
      }
      if (*name == "lxe_editor_wait_for") {
        const AutomationStateSnapshot state = captureFreshState();
        const std::string resource =
            scalarString(arguments, "resource").value_or("summary");
        const std::string contains = scalarString(arguments, "contains").value_or("");
        const std::string uri = resource.rfind("lxe-editor://", 0) == 0
                                    ? resource
                                    : "lxe-editor://" + resource;
        const std::string text = resourceJson(uri, state);
        const bool matched = contains.empty() ||
                             text.find(contains) != std::string::npos;
        const std::string structured =
            std::string("{\"matched\":") + (matched ? "true" : "false") +
            ",\"resource\":" + jsonQuoted(uri) + ",\"text\":" +
            jsonQuoted(text) + "}";
        frames.push_back(makeHeaderFrame(makeResultEnvelope(
            idJson,
            toolCallResult(matched ? "condition already matched"
                                   : "condition not yet matched",
                           structured, false))));
        continue;
      }
      if (*name == "lxe_editor_command") {
        const std::optional<std::string> line = scalarString(arguments, "line");
        if (!line.has_value()) {
          frames.push_back(makeHeaderFrame(
              makeErrorEnvelope(idJson, -32602, "missing line")));
          continue;
        }
        const AutomationCommandResponse response =
            service.executeCommand(AutomationCommandRequest{.line = *line});
        service.refresh();
        const std::string structured =
            response.structuredJson.empty() ? "{}" : response.structuredJson;
        frames.push_back(makeHeaderFrame(makeResultEnvelope(
            idJson, toolCallResult(response.message, structured, !response.ok))));
        continue;
      }
      if (*name == "lxe_editor_pick") {
        const std::optional<double> x = scalarNumber(arguments, "x");
        const std::optional<double> y = scalarNumber(arguments, "y");
        if (!x.has_value() || !y.has_value()) {
          frames.push_back(makeHeaderFrame(
              makeErrorEnvelope(idJson, -32602, "missing x or y")));
          continue;
        }
        const std::ostringstream lineBuilder = [&] {
          std::ostringstream out;
          out << "pick " << *x << " " << *y;
          return out;
        }();
        const AutomationCommandResponse response =
            service.executeCommand(AutomationCommandRequest{
                .line = lineBuilder.str(),
            });
        service.refresh();
        const std::string structured =
            response.structuredJson.empty() ? "{}" : response.structuredJson;
        frames.push_back(makeHeaderFrame(makeResultEnvelope(
            idJson, toolCallResult(response.message, structured, !response.ok))));
        continue;
      }

      frames.push_back(makeHeaderFrame(
          makeErrorEnvelope(idJson, -32601, "unknown tool: " + *name)));
      continue;
    }

    if (message->idJson.has_value()) {
      frames.push_back(makeHeaderFrame(
          makeErrorEnvelope(idJson, -32601, "unknown method: " + message->method)));
    }
  }

  if (frames.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_impl->mutex);
  for (std::string& frame : frames) {
    m_impl->outgoingFrames.push_back(std::move(frame));
  }
}

bool EditorMcpServer::isRunning() const {
  return m_impl->running;
}

const EditorMcpServerConfig& EditorMcpServer::config() const { return m_config; }

std::uint16_t EditorMcpServer::boundPort() const { return m_impl->boundPort; }

} // namespace LX_demo::lxe_editor
