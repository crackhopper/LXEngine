#include "demos/lxe_editor/lxe_editor_mcp_handler.hpp"

#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"
#include "demos/lxe_editor/editor_mcp_protocol.hpp"

#include "yaml-cpp/yaml.h"

#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {
namespace {

constexpr std::string_view kJsonRpcVersion = "2.0";
constexpr std::string_view kProtocolVersion = "2025-03-26";

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  return apiJsonEscape(text);
}

[[nodiscard]] std::string jsonQuoted(std::string_view text) {
  return std::string("\"") + jsonEscape(text) + "\"";
}

[[nodiscard]] std::string buildSummaryJson(const ApiStateSnapshot& state) {
  std::ostringstream out;
  out << "{\"sceneName\":\"" << jsonEscape(state.scene.sceneName) << "\""
      << ",\"currentDocumentPath\":\""
      << jsonEscape(state.scene.currentDocumentPath) << "\""
      << ",\"sourceKind\":\""
      << apiSceneSourceKindName(state.scene.sourceKind) << "\""
      << ",\"permission\":\""
      << apiPermissionLevelName(state.scene.permission) << "\""
      << ",\"dirty\":" << (state.scene.dirty ? "true" : "false")
      << ",\"previewEnabled\":"
      << (state.toolbar.previewEnabled ? "true" : "false")
      << ",\"debugEnabled\":"
      << (state.toolbar.debugEnabled ? "true" : "false")
      << ",\"mode\":\"" << apiEditorModeName(state.toolbar.mode) << "\""
      << ",\"camera\":\""
      << apiCameraControlModeName(state.toolbar.camera) << "\""
      << ",\"selectionCount\":" << state.selection.selectedPaths.size()
      << ",\"activeCameraPath\":\""
      << jsonEscape(state.cameras.activeCameraPath) << "\"}";
  return out.str();
}

[[nodiscard]] std::string resourceJson(const std::string_view uri,
                                       const ApiStateSnapshot& state) {
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

[[nodiscard]] std::string initializeResult() {
  return std::string("{\"protocolVersion\":\"") + std::string(kProtocolVersion) +
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
         jsonEscape(text) + "\",\"json\":" + text + "}]}";
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
        if (root["id"].Tag() == "!" ||
            (!asString.empty() &&
             !std::isdigit(static_cast<unsigned char>(asString.front())))) {
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

LxeEditorMcpResponse
handleLxeEditorMcpHttpRequest(std::string_view payload,
                              LxeEditorApiService& service) {
  std::string parseError;
  const auto message = parseMessage(std::string(payload), parseError);
  if (!message.has_value()) {
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeErrorEnvelope("null", -32700,
                                  "parse error: " + parseError),
    };
  }

  if (!message->idJson.has_value() &&
      message->method == "notifications/initialized") {
    return LxeEditorMcpResponse{.hasBody = false, .body = {}};
  }

  const std::string idJson = message->idJson.value_or("null");
  const auto captureFreshState = [&service]() { return service.captureState(); };

  if (message->method == "initialize") {
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(idJson, initializeResult()),
    };
  }

  if (message->method == "ping") {
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(idJson, "{}"),
    };
  }
  if (message->method == "tools/list") {
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(idJson, toolsListResult()),
    };
  }
  if (message->method == "resources/list") {
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(idJson, resourcesListResult()),
    };
  }
  if (message->method == "prompts/list") {
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(idJson, "{\"prompts\":[]}"),
    };
  }
  if (message->method == "resources/read") {
    const ApiStateSnapshot state = captureFreshState();
    const YAML::Node params = message->paramsJson.empty()
                                  ? YAML::Node{}
                                  : YAML::Load(message->paramsJson);
    const std::string uri =
        scalarString(params, "uri").value_or("lxe-editor://summary");
    const std::string text = resourceJson(uri, state);
    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(idJson, resourceReadResult(uri, text)),
    };
  }
  if (message->method == "tools/call") {
    const YAML::Node params = message->paramsJson.empty()
                                  ? YAML::Node{}
                                  : YAML::Load(message->paramsJson);
    const std::string name =
        scalarString(params, "name").value_or(std::string{});
    const YAML::Node arguments = params["arguments"];

    if (name == "lxe_editor_command") {
      const std::string line =
          scalarString(arguments, "line").value_or(std::string{});
      const ApiCommandResponse response =
          service.executeCommand(ApiCommandRequest{.line = line});
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult(response.message, toJson(response), !response.ok)),
      };
    }
    if (name == "lxe_editor_get_summary") {
      const ApiStateSnapshot state = captureFreshState();
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult("summary", buildSummaryJson(state), false)),
      };
    }
    if (name == "lxe_editor_get_selection") {
      const ApiStateSnapshot state = captureFreshState();
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult("selection", toJson(state.selection), false)),
      };
    }
    if (name == "lxe_editor_get_cameras") {
      const ApiStateSnapshot state = captureFreshState();
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult("cameras", toJson(state.cameras), false)),
      };
    }
    if (name == "lxe_editor_pick") {
      const auto x = scalarNumber(arguments, "x");
      const auto y = scalarNumber(arguments, "y");
      if (!x.has_value() || !y.has_value()) {
        return LxeEditorMcpResponse{
            .hasBody = true,
            .body = makeResultEnvelope(
                idJson,
                toolCallResult("pick requires x and y", {}, true)),
        };
      }
      std::ostringstream line;
      line << "pick " << *x << " " << *y;
      const ApiCommandResponse response =
          service.executeCommand(ApiCommandRequest{.line = line.str()});
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult(response.message, toJson(response), !response.ok)),
      };
    }
    if (name == "lxe_editor_wait_for") {
      const ApiStateSnapshot state = captureFreshState();
      const std::string resource = scalarString(arguments, "resource")
                                       .value_or("lxe-editor://summary");
      const std::string expected =
          scalarString(arguments, "contains").value_or(std::string{});
      const std::string text = resourceJson(resource, state);
      const bool matches = expected.empty() ||
                           text.find(expected) != std::string::npos;
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult(matches ? "matched" : "not matched",
                             std::string("{\"matched\":") +
                                 (matches ? "true" : "false") +
                                 ",\"resource\":\"" + jsonEscape(resource) +
                                 "\",\"text\":" + jsonQuoted(text) + "}")),
      };
    }
    if (name == "lxe_editor_ensure_running") {
      const ApiStateSnapshot state = captureFreshState();
      return LxeEditorMcpResponse{
          .hasBody = true,
          .body = makeResultEnvelope(
              idJson,
              toolCallResult("running", buildSummaryJson(state), false)),
      };
    }

    return LxeEditorMcpResponse{
        .hasBody = true,
        .body = makeResultEnvelope(
            idJson,
            toolCallResult("unknown tool: " + name, {}, true)),
    };
  }

  return LxeEditorMcpResponse{
      .hasBody = true,
      .body = makeErrorEnvelope(idJson, -32601, "method not found"),
  };
}

} // namespace LX_demo::lxe_editor
