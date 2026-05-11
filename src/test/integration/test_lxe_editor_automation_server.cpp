#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/editor_automation_server.hpp"
#include "demos/lxe_editor/editor_automation_service.hpp"
#include "demos/lxe_editor/runtime_state.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace LX_core;
using namespace LX_demo::lxe_editor;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

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

struct Fixture final {
  SceneSharedPtr scene = Scene::create(nullptr);
  EditorState editorState;
  CommandBus bus;
  LxeEditorApiService::Hooks hooks;
  std::unique_ptr<LxeEditorApiService> service;
  std::unique_ptr<LxeEditorApiServer> server;

  Fixture() {
    hooks.sceneSummary = [] {
      return AutomationSceneSummary{
          .sceneName = "Scene",
          .currentDocumentPath = "data/scenes/test.scene.yaml",
          .sourceKind = AutomationSceneSourceKind::Local,
          .permission = AutomationPermissionLevel::User,
          .dirty = false,
      };
    };
    hooks.toolbarSnapshot = [] {
      return AutomationToolbarSnapshot{
          .editMode = AutomationEditMode::Orbit,
          .previewEnabled = false,
      };
    };
    service =
        std::make_unique<LxeEditorApiService>(bus, editorState, *scene, hooks);
    bus.registerHandler("echo", "echo <value>", [](std::vector<std::string> args) {
      return CommandResult{true, args.empty() ? std::string{} : args.front(),
                           "{\"kind\":\"echo\"}"};
    });

    server = std::make_unique<LxeEditorApiServer>(
        LxeEditorApiServerConfig{
            .enabled = true,
            .host = "127.0.0.1",
            .port = 0,
            .token = "secret-token",
        });
    std::string error;
    if (!server->start(&error)) {
      throw std::runtime_error("failed to start API server: " + error);
    }
  }

  ~Fixture() { server->stop(); }
};

[[nodiscard]] SocketHandle connectClient(const std::uint16_t port) {
  const SocketHandle socketHandle = socket(AF_INET, SOCK_STREAM, 0);
  if (socketHandle == kInvalidSocket) {
    return kInvalidSocket;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(socketHandle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
      0) {
    closeSocket(socketHandle);
    return kInvalidSocket;
  }
  return socketHandle;
}

bool sendAll(const SocketHandle socketHandle, const std::string& text) {
  size_t offset = 0;
  while (offset < text.size()) {
    const int sent = send(socketHandle, text.data() + offset,
                          static_cast<int>(text.size() - offset), 0);
    if (sent <= 0) {
      return false;
    }
    offset += static_cast<size_t>(sent);
  }
  return true;
}

std::string recvSome(const SocketHandle socketHandle) {
  char buffer[4096];
#if defined(_WIN32)
  u_long mode = 1;
  ioctlsocket(socketHandle, FIONBIO, &mode);
  const int flags = 0;
#else
  const int flags = MSG_DONTWAIT;
#endif
  const int count =
      recv(socketHandle, buffer, static_cast<int>(sizeof(buffer)), flags);
  if (count <= 0) {
    return {};
  }
  return std::string(buffer, buffer + count);
}

template <typename ServiceT, typename HttpServerT>
std::string pumpUntilRead(ServiceT& service, HttpServerT* server,
                          const SocketHandle socketHandle,
                          const int maxIterations = 200) {
  std::string received;
  for (int i = 0; i < maxIterations; ++i) {
    service.refresh();
    if (server != nullptr) {
      server->pump(service);
    }
    received += recvSome(socketHandle);
    if (!received.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return received;
}

std::string makeHttpPostRequest(const std::string& path,
                                const std::string& body,
                                const bool authorized = true) {
  std::string request = "POST " + path + " HTTP/1.1\r\nHost: localhost\r\n";
  if (authorized) {
    request += "Authorization: Bearer secret-token\r\n";
  }
  request += "Content-Type: application/json\r\nContent-Length: " +
             std::to_string(body.size()) + "\r\n\r\n" + body;
  return request;
}

std::string makeMaskedWsFrame(const std::string& payload) {
  const std::array<std::uint8_t, 4> mask = {0x12, 0x34, 0x56, 0x78};
  std::string out;
  out.push_back(static_cast<char>(0x81));
  out.push_back(static_cast<char>(0x80 | payload.size()));
  for (const auto value : mask) {
    out.push_back(static_cast<char>(value));
  }
  for (size_t i = 0; i < payload.size(); ++i) {
    out.push_back(
        static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ mask[i % 4]));
  }
  return out;
}

void testHttpAuthorizationAndCommandRoundTrip() {
  Fixture fixture;

  const SocketHandle unauthorized = connectClient(fixture.server->boundPort());
  EXPECT(unauthorized != kInvalidSocket, "unauthorized client should connect");
  if (unauthorized == kInvalidSocket) {
    return;
  }
  const std::string unauthorizedRequest =
      "GET /api/state/summary HTTP/1.1\r\nHost: localhost\r\n\r\n";
  EXPECT(sendAll(unauthorized, unauthorizedRequest),
         "unauthorized request should send");
  const std::string unauthorizedResponse =
      pumpUntilRead(*fixture.service, fixture.server.get(), unauthorized);
  EXPECT(unauthorizedResponse.find("401 Unauthorized") != std::string::npos,
         "missing token should produce 401");
  closeSocket(unauthorized);

  const SocketHandle authorized = connectClient(fixture.server->boundPort());
  EXPECT(authorized != kInvalidSocket, "authorized client should connect");
  if (authorized == kInvalidSocket) {
    return;
  }
  const std::string body = "{\"line\":\"echo hello\"}";
  const std::string request =
      "POST /api/command HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer "
      "secret-token\r\nContent-Type: application/json\r\nContent-Length: " +
      std::to_string(body.size()) + "\r\n\r\n" + body;
  EXPECT(sendAll(authorized, request), "authorized request should send");
  const std::string response =
      pumpUntilRead(*fixture.service, fixture.server.get(), authorized);
  EXPECT(response.find("200 OK") != std::string::npos,
         "authorized command should return 200");
  EXPECT(response.find("\"message\":\"hello\"") != std::string::npos,
         "authorized command should surface command response");
  closeSocket(authorized);
}

void testWebSocketHandshakeAndEvents() {
  Fixture fixture;

  const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
  EXPECT(socketHandle != kInvalidSocket, "websocket client should connect");
  if (socketHandle == kInvalidSocket) {
    return;
  }

  const std::string handshake =
      "GET /ws?token=secret-token HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
  EXPECT(sendAll(socketHandle, handshake), "websocket handshake should send");
  const std::string handshakeResponse =
      pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
  EXPECT(handshakeResponse.find("101 Switching Protocols") != std::string::npos,
         "websocket handshake should succeed");

  const std::string commandFrame =
      makeMaskedWsFrame("{\"type\":\"command\",\"line\":\"echo websocket\"}");
  EXPECT(sendAll(socketHandle, commandFrame), "websocket command frame should send");
  const std::string commandResponse =
      pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
  EXPECT(commandResponse.find("command.response") != std::string::npos,
         "websocket command should return command response envelope");
  EXPECT(commandResponse.find("command.executed") != std::string::npos,
         "websocket command should also push command.executed event");
  closeSocket(socketHandle);
}

void testRuntimeStateRoundTripsYaml() {
  const auto root = std::filesystem::temp_directory_path() /
                    "lxengine_lxe_editor_runtime_state";
  std::filesystem::remove_all(root);

  const LxeEditorRuntimeState expected{
      .pid = 1234,
      .httpHost = "0.0.0.0",
      .httpPort = 3768,
      .wsHost = "0.0.0.0",
      .wsPort = 3768,
      .mcpUrl = "http://127.0.0.1:3768/mcp",
      .tokenFile = (root / "automation_token.txt").string(),
      .startedAt = "2026-05-11-160000",
  };

  saveLxeEditorRuntimeState(root, expected);
  const auto loaded = loadLxeEditorRuntimeState(root);
  EXPECT(loaded.has_value(), "runtime state should reload from yaml");
  EXPECT(*loaded == expected, "runtime state yaml should round-trip");
}

void testMcpInitializeAndTools() {
  Fixture fixture;

  const auto roundTrip = [&](const std::string& requestBody) -> std::string {
    const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
    EXPECT(socketHandle != kInvalidSocket, "MCP HTTP client should connect");
    if (socketHandle == kInvalidSocket) {
      return {};
    }
    const std::string request = makeHttpPostRequest("/mcp", requestBody);
    EXPECT(sendAll(socketHandle, request), "MCP HTTP request should send");
    const std::string response =
        pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
    closeSocket(socketHandle);
    return response;
  };

  const std::string initializeResponse = roundTrip(
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
      "\"params\":{\"protocolVersion\":\"1999-01-01\"}}");
  if (initializeResponse.empty()) {
    return;
  }
  EXPECT(initializeResponse.find("200 OK") != std::string::npos,
         "MCP initialize should return 200");
  EXPECT(initializeResponse.find("\"protocolVersion\":\"2025-03-26\"") !=
             std::string::npos,
         "MCP initialize should advertise the supported protocol version");
  EXPECT(initializeResponse.find("\"name\":\"lxe_editor\"") !=
             std::string::npos,
         "MCP initialize should advertise lxe_editor server name");

  const std::string toolsResponse = roundTrip(
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\","
      "\"params\":{}}");
  EXPECT(toolsResponse.find("lxe_editor_command") != std::string::npos,
         "MCP tools/list should advertise lxe_editor_command");
  EXPECT(toolsResponse.find("lxe_editor_pick") != std::string::npos,
         "MCP tools/list should advertise lxe_editor_pick");

  const std::string summaryResponse = roundTrip(
      "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
      "\"params\":{\"name\":\"lxe_editor_get_summary\","
      "\"arguments\":{}}}");
  EXPECT(summaryResponse.find("\"sceneName\":\"Scene\"") != std::string::npos,
         "MCP summary tool should return scene summary");
}

void testHttpMcpRequiresBearerToken() {
  Fixture fixture;

  const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
  EXPECT(socketHandle != kInvalidSocket, "MCP HTTP client should connect");
  if (socketHandle == kInvalidSocket) {
    return;
  }

  const std::string unauthorizedInitialize = makeHttpPostRequest(
      "/mcp",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
      "\"params\":{\"protocolVersion\":\"2025-03-26\"}}",
      false);
  EXPECT(sendAll(socketHandle, unauthorizedInitialize),
         "unauthorized MCP initialize should send");
  const std::string unauthorizedResponse =
      pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
  EXPECT(unauthorizedResponse.find("401 Unauthorized") != std::string::npos,
         "HTTP MCP should reject missing bearer token");
  EXPECT(unauthorizedResponse.find("unauthorized") != std::string::npos,
         "HTTP MCP should describe unauthorized access");
  closeSocket(socketHandle);
}

} // namespace

int main() {
  testHttpAuthorizationAndCommandRoundTrip();
  testWebSocketHandshakeAndEvents();
  testRuntimeStateRoundTripsYaml();
  testMcpInitializeAndTools();
  testHttpMcpRequiresBearerToken();

  if (failures != 0) {
    std::cerr << failures << " API server test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor API server tests\n";
  return 0;
}
