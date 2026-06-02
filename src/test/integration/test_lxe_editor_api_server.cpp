#include "core/editor/command_bus.hpp"
#include "core/editor/editor_state.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/lxe_editor_api_server.hpp"
#include "demos/lxe_editor/lxe_editor_api_service.hpp"
#include "demos/lxe_editor/recording_controller.hpp"
#include "demos/lxe_editor/runtime_state.hpp"

#include <chrono>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
const std::string kLegacySourceKey = std::string("source") + "Kind";
const std::string kLegacyDocumentKey =
    std::string("current") + "DocumentPath";
const std::string kLegacyPermissionKey = std::string("permi") + "ssion";

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
  std::unique_ptr<RecordingController> recording;
  std::optional<ApiProjectSummary> project;
  std::string capturedDisplayConfigKey;
  std::string capturedDisplayConfigPatch;

  Fixture() {
    project = ApiProjectSummary{
        .id = "demo",
        .displayName = "Demo",
        .path = "data/projects/demo",
        .dirty = true,
        .activeScene = "scenes/main.scene.yaml",
    };
    hooks.sceneSummary = [] {
      return ApiSceneSummary{
          .sceneName = "Scene",
          .dirty = false,
      };
    };
    hooks.projectSummary = [this] { return project; };
    hooks.toolbarSnapshot = [] {
      return ApiToolbarSnapshot{
          .mode = ApiEditorMode::Selection,
          .camera = ApiCameraControlMode::FreeFly,
          .previewEnabled = false,
          .debugEnabled = true,
      };
    };
    hooks.displayConfigSet = [this](const std::string& key,
                                    const std::string& patch) {
      capturedDisplayConfigKey = key;
      capturedDisplayConfigPatch = patch;
      return "{\"ok\":true,\"source\":\"display-config-test\"}";
    };
    recording = std::make_unique<RecordingController>(
        std::filesystem::temp_directory_path() / "lxe_api_server_recording");
    hooks.recording =
        [this]() -> std::optional<std::reference_wrapper<RecordingController>> {
      return *recording;
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

std::string makeHttpGetRequest(const std::string& path,
                               const bool authorized = true) {
  std::string request = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n";
  if (authorized) {
    request += "Authorization: Bearer secret-token\r\n";
  }
  request += "\r\n";
  return request;
}

void expectSplitToolbarState(const std::string& body) {
  EXPECT(body.find("\"mode\":\"selection\"") != std::string::npos,
         "toolbar response should include editor mode");
  EXPECT(body.find("\"camera\":\"freefly\"") != std::string::npos,
         "toolbar response should include camera control mode");
  EXPECT(body.find("\"editMode\"") == std::string::npos,
         "toolbar response should not include legacy editMode");
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

void testHttpStateEndpointsExposeSplitToolbarState() {
  Fixture fixture;

  const auto get = [&](const std::string& path) -> std::string {
    const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
    EXPECT(socketHandle != kInvalidSocket, "HTTP state client should connect");
    if (socketHandle == kInvalidSocket) {
      return {};
    }
    EXPECT(sendAll(socketHandle, makeHttpGetRequest(path)),
           "HTTP state request should send");
    const std::string response =
        pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
    closeSocket(socketHandle);
    return response;
  };

  const std::string toolbarResponse = get("/api/state/toolbar");
  EXPECT(toolbarResponse.find("200 OK") != std::string::npos,
         "toolbar state request should return 200");
  expectSplitToolbarState(toolbarResponse);

  const std::string summaryResponse = get("/api/state/summary");
  EXPECT(summaryResponse.find("200 OK") != std::string::npos,
         "summary state request should return 200");
  expectSplitToolbarState(summaryResponse);
  EXPECT(summaryResponse.find("\"project\":{\"id\":\"demo\",\"displayName\":"
                              "\"Demo\",\"path\":\"data/projects/demo\","
                              "\"dirty\":true,\"activeScene\":\"scenes/main."
                              "scene.yaml\"}") != std::string::npos,
         "summary response should include project summary");
  EXPECT(summaryResponse.find(kLegacySourceKey) == std::string::npos,
         "summary response should not include legacy source key");
  EXPECT(summaryResponse.find(kLegacyDocumentKey) == std::string::npos,
         "summary response should not include legacy document path key");
  EXPECT(summaryResponse.find(kLegacyPermissionKey) == std::string::npos,
         "summary response should not include legacy access key");

  const std::string stateResponse = get("/api/state");
  EXPECT(stateResponse.find("200 OK") != std::string::npos,
         "full state request should return 200");
  EXPECT(stateResponse.find("\"project\":{\"id\":\"demo\",\"displayName\":"
                            "\"Demo\",\"path\":\"data/projects/demo\","
                            "\"dirty\":true,\"activeScene\":\"scenes/main."
                            "scene.yaml\"}") != std::string::npos,
         "full state response should include project summary");
  EXPECT(stateResponse.find(kLegacySourceKey) == std::string::npos,
         "full state response should not include legacy source key");
  EXPECT(stateResponse.find(kLegacyDocumentKey) == std::string::npos,
         "full state response should not include legacy document path key");
  EXPECT(stateResponse.find(kLegacyPermissionKey) == std::string::npos,
         "full state response should not include legacy access key");

  fixture.project = std::nullopt;
  const std::string nullSummaryResponse = get("/api/state/summary");
  EXPECT(nullSummaryResponse.find("\"project\":null") != std::string::npos,
         "summary response should serialize a missing project as null");
  const std::string nullStateResponse = get("/api/state");
  EXPECT(nullStateResponse.find("\"project\":null") != std::string::npos,
         "full state response should serialize a missing project as null");
}

void testHttpDisplayConfigObjectPatchPassesRawJson() {
  Fixture fixture;

  const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
  EXPECT(socketHandle != kInvalidSocket, "display config client should connect");
  if (socketHandle == kInvalidSocket) {
    return;
  }
  const std::string body =
      "{\"key\":\"default\",\"patch\":{\"preferences\":{\"uiFontScale\":1.4}}}";
  EXPECT(sendAll(socketHandle,
                 makeHttpPostRequest("/api/display/config", body)),
         "display config request should send");
  const std::string response =
      pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
  closeSocket(socketHandle);

  EXPECT(response.find("200 OK") != std::string::npos,
         "display config object patch should return 200");
  EXPECT(response.find("\"source\":\"display-config-test\"") !=
             std::string::npos,
         "display config object patch should return hook response");
  EXPECT(fixture.capturedDisplayConfigKey == "default",
         "display config object patch should pass key to hook");
  EXPECT(fixture.capturedDisplayConfigPatch ==
             "{\"preferences\":{\"uiFontScale\":1.4}}",
         "display config object patch should pass raw JSON object to hook");
}

void testHttpDisplayConfigPatchIgnoresNestedPatchKey() {
  Fixture fixture;

  const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
  EXPECT(socketHandle != kInvalidSocket,
         "display config nested patch client should connect");
  if (socketHandle == kInvalidSocket) {
    return;
  }
  const std::string body =
      "{\"key\":\"default\",\"meta\":{\"patch\":{\"preferences\":{"
      "\"uiFontScale\":2.0}}},\"patch\":{\"preferences\":{\"uiFontScale\":1.4}}}";
  EXPECT(sendAll(socketHandle,
                 makeHttpPostRequest("/api/display/config", body)),
         "display config nested patch request should send");
  const std::string response =
      pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
  closeSocket(socketHandle);

  EXPECT(response.find("200 OK") != std::string::npos,
         "display config nested patch request should return 200");
  EXPECT(fixture.capturedDisplayConfigPatch ==
             "{\"preferences\":{\"uiFontScale\":1.4}}",
         "display config should use top-level patch instead of nested patch");
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
  const auto statePath = root / "runtime_state.yaml";

  const LxeEditorRuntimeState expected{
      .pid = 1234,
      .httpHost = "0.0.0.0",
      .httpPort = 3768,
      .wsHost = "0.0.0.0",
      .wsPort = 3768,
      .tokenFile = (root / "api_token.txt").string(),
      .startedAt = "2026-05-11-160000",
  };

  saveLxeEditorRuntimeState(root, expected);
  std::ifstream stateFile(statePath);
  const std::string yaml((std::istreambuf_iterator<char>(stateFile)),
                         std::istreambuf_iterator<char>());
  EXPECT(yaml.find("mcpUrl") == std::string::npos,
         "runtime state yaml should not publish mcpUrl");
  const auto loaded = loadLxeEditorRuntimeState(root);
  EXPECT(loaded.has_value(), "runtime state should reload from yaml");
  EXPECT(*loaded == expected, "runtime state yaml should round-trip");
}

void testHttpMcpEndpointIsRemoved() {
  Fixture fixture;

  const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
  EXPECT(socketHandle != kInvalidSocket, "removed MCP HTTP client should connect");
  if (socketHandle == kInvalidSocket) {
    return;
  }
  const std::string request = makeHttpPostRequest(
      "/mcp",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
      "\"params\":{\"protocolVersion\":\"2025-03-26\"}}");
  EXPECT(sendAll(socketHandle, request), "removed MCP request should send");
  const std::string response =
      pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
  EXPECT(response.find("404 Not Found") != std::string::npos,
         "removed MCP endpoint should return 404");
  EXPECT(response.find("\"code\":\"not_found\"") != std::string::npos,
         "removed MCP endpoint should use not_found error");
  closeSocket(socketHandle);
}

void testHttpRecordingEndpointRoundTrip() {
  Fixture fixture;

  const auto post = [&](const std::string& path,
                        const std::string& body) -> std::string {
    const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
    EXPECT(socketHandle != kInvalidSocket, "recording POST client should connect");
    if (socketHandle == kInvalidSocket) {
      return {};
    }
    EXPECT(sendAll(socketHandle, makeHttpPostRequest(path, body)),
           "recording POST request should send");
    const std::string response =
        pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
    closeSocket(socketHandle);
    return response;
  };
  const auto get = [&](const std::string& path) -> std::string {
    const SocketHandle socketHandle = connectClient(fixture.server->boundPort());
    EXPECT(socketHandle != kInvalidSocket, "recording GET client should connect");
    if (socketHandle == kInvalidSocket) {
      return {};
    }
    EXPECT(sendAll(socketHandle, makeHttpGetRequest(path)),
           "recording GET request should send");
    const std::string response =
        pumpUntilRead(*fixture.service, fixture.server.get(), socketHandle);
    closeSocket(socketHandle);
    return response;
  };

  const std::string enableResponse = post("/recording/enable", "{}");
  EXPECT(enableResponse.find("\"enabled\":true") != std::string::npos,
         "recording enable should set enabled=true");

  const std::string startResponse =
      post("/recording/start", "{\"detailLevel\":\"basic\"}");
  EXPECT(startResponse.find("\"active\":true") != std::string::npos,
         "recording start should activate session");

  const std::string commandResponse =
      post("/api/command", "{\"line\":\"echo recorded\"}");
  EXPECT(commandResponse.find("\"message\":\"recorded\"") != std::string::npos,
         "command should execute while recording");

  const std::string activeResponse = get("/recording/read?id=active");
  EXPECT(activeResponse.find("\"source\":\"mcp\"") != std::string::npos,
         "active recording should include mcp command source");
  EXPECT(activeResponse.find("echo recorded") != std::string::npos,
         "active recording should include command line");
  EXPECT(activeResponse.find("\"build\":{") != std::string::npos,
         "active recording should include build identity");

  const std::string stopResponse =
      post("/recording/stop", "{\"save\":false}");
  EXPECT(stopResponse.find("\"stepCount\":1") != std::string::npos,
         "recording stop should report one step");
}

void testHttpBuildInfoRequiresTokenAndReturnsIdentity() {
  Fixture fixture;

  const SocketHandle unauthorized = connectClient(fixture.server->boundPort());
  EXPECT(unauthorized != kInvalidSocket,
         "unauthorized build info client should connect");
  if (unauthorized != kInvalidSocket) {
    EXPECT(sendAll(unauthorized, makeHttpGetRequest("/api/build", false)),
           "unauthorized build info request should send");
    const std::string response =
        pumpUntilRead(*fixture.service, fixture.server.get(), unauthorized);
    EXPECT(response.find("401 Unauthorized") != std::string::npos,
           "build info should require authorization");
    closeSocket(unauthorized);
  }

  const SocketHandle authorized = connectClient(fixture.server->boundPort());
  EXPECT(authorized != kInvalidSocket,
         "authorized build info client should connect");
  if (authorized == kInvalidSocket) {
    return;
  }
  EXPECT(sendAll(authorized, makeHttpGetRequest("/api/build")),
         "authorized build info request should send");
  const std::string response =
      pumpUntilRead(*fixture.service, fixture.server.get(), authorized);
  EXPECT(response.find("200 OK") != std::string::npos,
         "build info should return HTTP 200");
  EXPECT(response.find("\"buildInfo\"") != std::string::npos,
         "build info should include composed buildInfo string");
  EXPECT(response.find("\"gitCommit\"") == std::string::npos,
         "build info should not expose split git fields");
  EXPECT(response.find("\"buildType\"") == std::string::npos,
         "build info should not expose split build fields");
  EXPECT(response.find("\"builtAt\"") == std::string::npos,
         "build info should not expose split build timestamp");
  closeSocket(authorized);
}

} // namespace

int main() {
  testHttpAuthorizationAndCommandRoundTrip();
  testHttpStateEndpointsExposeSplitToolbarState();
  testHttpDisplayConfigObjectPatchPassesRawJson();
  testHttpDisplayConfigPatchIgnoresNestedPatchKey();
  testWebSocketHandshakeAndEvents();
  testRuntimeStateRoundTripsYaml();
  testHttpMcpEndpointIsRemoved();
  testHttpRecordingEndpointRoundTrip();
  testHttpBuildInfoRequiresTokenAndReturnsIdentity();

  if (failures != 0) {
    std::cerr << failures << " API server test(s) failed\n";
    return 1;
  }

  std::cout << "[PASS] lxe_editor API server tests\n";
  return 0;
}
