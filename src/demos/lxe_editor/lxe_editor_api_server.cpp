#include "demos/lxe_editor/lxe_editor_api_server.hpp"

#include "demos/lxe_editor/lxe_editor_api_protocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
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

[[nodiscard]] std::string trim(std::string text) {
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                           text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t' ||
                                 text[start] == '\r' || text[start] == '\n')) {
    ++start;
  }
  return text.substr(start);
}

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

[[nodiscard]] std::optional<std::string>
headerValue(const std::unordered_map<std::string, std::string> &headers,
            const std::string &key) {
  const auto it = headers.find(key);
  if (it == headers.end()) {
    return std::nullopt;
  }
  return it->second;
}

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char c) { return std::tolower(c); });
  return text;
}

[[nodiscard]] std::optional<std::string>
jsonStringField(const std::string &body, const std::string &key) {
  const std::string needle = "\"" + key + "\"";
  const size_t keyPos = body.find(needle);
  if (keyPos == std::string::npos) {
    return std::nullopt;
  }
  const size_t colonPos = body.find(':', keyPos + needle.size());
  if (colonPos == std::string::npos) {
    return std::nullopt;
  }
  const size_t firstQuote = body.find('"', colonPos + 1);
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }
  std::string value;
  bool escaping = false;
  for (size_t i = firstQuote + 1; i < body.size(); ++i) {
    const char c = body[i];
    if (escaping) {
      switch (c) {
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        value.push_back(c);
        break;
      }
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') {
      return value;
    }
    value.push_back(c);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
jsonStringOrRawContainerField(const std::string &body,
                              const std::string &key) {
  size_t pos = 0;
  while (pos < body.size() &&
         (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' ||
          body[pos] == '\n')) {
    ++pos;
  }
  if (pos >= body.size() || body[pos] != '{') {
    return std::nullopt;
  }
  ++pos;
  bool inString = false;
  bool escaping = false;
  while (pos < body.size()) {
    while (pos < body.size() &&
           (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' ||
            body[pos] == '\n' || body[pos] == ',')) {
      ++pos;
    }
    if (pos >= body.size() || body[pos] == '}') {
      return std::nullopt;
    }
    if (body[pos] != '"') {
      return std::nullopt;
    }

    std::string memberKey;
    escaping = false;
    for (++pos; pos < body.size(); ++pos) {
      const char c = body[pos];
      if (escaping) {
        memberKey.push_back(c);
        escaping = false;
        continue;
      }
      if (c == '\\') {
        escaping = true;
        continue;
      }
      if (c == '"') {
        ++pos;
        break;
      }
      memberKey.push_back(c);
    }
    while (pos < body.size() &&
           (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' ||
            body[pos] == '\n')) {
      ++pos;
    }
    if (pos >= body.size() || body[pos] != ':') {
      return std::nullopt;
    }
    ++pos;
    while (pos < body.size() &&
           (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\r' ||
            body[pos] == '\n')) {
      ++pos;
    }
    if (pos >= body.size()) {
      return std::nullopt;
    }
    const size_t valuePos = pos;
    if (body[pos] == '"') {
      std::string value;
      escaping = false;
      for (++pos; pos < body.size(); ++pos) {
        const char c = body[pos];
        if (escaping) {
          switch (c) {
          case 'n':
            value.push_back('\n');
            break;
          case 'r':
            value.push_back('\r');
            break;
          case 't':
            value.push_back('\t');
            break;
          default:
            value.push_back(c);
            break;
          }
          escaping = false;
          continue;
        }
        if (c == '\\') {
          escaping = true;
          continue;
        }
        if (c == '"') {
          ++pos;
          if (memberKey == key) {
            return value;
          }
          break;
        }
        value.push_back(c);
      }
      continue;
    }

    const char opener = body[valuePos];
    const char closer = opener == '{' ? '}' : opener == '[' ? ']' : '\0';
    if (closer == '\0') {
      return std::nullopt;
    }
    inString = false;
    escaping = false;
    int valueDepth = 0;
    for (; pos < body.size(); ++pos) {
      const char c = body[pos];
      if (inString) {
        if (escaping) {
          escaping = false;
          continue;
        }
        if (c == '\\') {
          escaping = true;
          continue;
        }
        if (c == '"') {
          inString = false;
        }
        continue;
      }
      if (c == '"') {
        inString = true;
        continue;
      }
      if (c == opener) {
        ++valueDepth;
        continue;
      }
      if (c == closer) {
        --valueDepth;
        if (valueDepth == 0) {
          ++pos;
          if (memberKey == key) {
            return body.substr(valuePos, pos - valuePos);
          }
          break;
        }
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<bool> jsonBoolField(const std::string &body,
                                                const std::string &key) {
  const std::string needle = "\"" + key + "\"";
  const size_t keyPos = body.find(needle);
  if (keyPos == std::string::npos) {
    return std::nullopt;
  }
  const size_t colonPos = body.find(':', keyPos + needle.size());
  if (colonPos == std::string::npos) {
    return std::nullopt;
  }
  const std::string tail = trim(body.substr(colonPos + 1));
  if (tail.rfind("true", 0) == 0) {
    return true;
  }
  if (tail.rfind("false", 0) == 0) {
    return false;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<double> jsonNumberField(const std::string &body,
                                                    const std::string &key) {
  const std::string needle = "\"" + key + "\"";
  const size_t keyPos = body.find(needle);
  if (keyPos == std::string::npos) {
    return std::nullopt;
  }
  const size_t colonPos = body.find(':', keyPos + needle.size());
  if (colonPos == std::string::npos) {
    return std::nullopt;
  }
  try {
    size_t consumed = 0;
    const std::string numberText = trim(body.substr(colonPos + 1));
    const double value = std::stod(numberText, &consumed);
    (void)consumed;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::string
httpResponse(std::string_view status, std::string_view body,
             std::string_view contentType = "application/json") {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << "\r\n"
      << "Content-Type: " << contentType << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  return out.str();
}

[[nodiscard]] std::string summaryJson(const ApiStateSnapshot &state) {
  std::ostringstream out;
  out << "{\"sceneName\":\"" << apiJsonEscape(state.scene.sceneName) << "\""
      << ",\"currentDocumentPath\":\""
      << apiJsonEscape(state.scene.currentDocumentPath) << "\""
      << ",\"sourceKind\":\"" << apiSceneSourceKindName(state.scene.sourceKind)
      << "\"" << ",\"permission\":\""
      << apiPermissionLevelName(state.scene.permission) << "\""
      << ",\"dirty\":" << (state.scene.dirty ? "true" : "false")
      << ",\"previewEnabled\":"
      << (state.toolbar.previewEnabled ? "true" : "false")
      << ",\"debugEnabled\":" << (state.toolbar.debugEnabled ? "true" : "false")
      << ",\"mode\":\"" << apiEditorModeName(state.toolbar.mode) << "\""
      << ",\"camera\":\"" << apiCameraControlModeName(state.toolbar.camera)
      << "\"" << ",\"selectionCount\":" << state.selection.selectedPaths.size()
      << ",\"activeCameraPath\":\""
      << apiJsonEscape(state.cameras.activeCameraPath) << "\"}";
  return out.str();
}

[[nodiscard]] std::array<std::uint8_t, 20> sha1(std::string_view text) {
  std::vector<std::uint8_t> bytes(text.begin(), text.end());
  const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;
  bytes.push_back(0x80u);
  while ((bytes.size() % 64u) != 56u) {
    bytes.push_back(0x00u);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffu));
  }

  std::uint32_t h0 = 0x67452301u;
  std::uint32_t h1 = 0xEFCDAB89u;
  std::uint32_t h2 = 0x98BADCFEu;
  std::uint32_t h3 = 0x10325476u;
  std::uint32_t h4 = 0xC3D2E1F0u;

  for (size_t chunk = 0; chunk < bytes.size(); chunk += 64) {
    std::uint32_t w[80] = {};
    for (int i = 0; i < 16; ++i) {
      const size_t offset = chunk + static_cast<size_t>(i) * 4u;
      w[i] = (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
             static_cast<std::uint32_t>(bytes[offset + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      const std::uint32_t value = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
      w[i] = (value << 1u) | (value >> 31u);
    }

    std::uint32_t a = h0;
    std::uint32_t b = h1;
    std::uint32_t c = h2;
    std::uint32_t d = h3;
    std::uint32_t e = h4;

    for (int i = 0; i < 80; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const std::uint32_t rolA = (a << 5u) | (a >> 27u);
      const std::uint32_t temp = rolA + f + e + k + w[i];
      e = d;
      d = c;
      c = (b << 30u) | (b >> 2u);
      b = a;
      a = temp;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<std::uint8_t, 20> digest{};
  const std::array<std::uint32_t, 5> words = {h0, h1, h2, h3, h4};
  for (size_t i = 0; i < words.size(); ++i) {
    digest[i * 4] = static_cast<std::uint8_t>((words[i] >> 24u) & 0xffu);
    digest[i * 4 + 1] = static_cast<std::uint8_t>((words[i] >> 16u) & 0xffu);
    digest[i * 4 + 2] = static_cast<std::uint8_t>((words[i] >> 8u) & 0xffu);
    digest[i * 4 + 3] = static_cast<std::uint8_t>(words[i] & 0xffu);
  }
  return digest;
}

[[nodiscard]] std::string base64Encode(const std::uint8_t *data,
                                       const size_t size) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((size + 2) / 3) * 4);
  for (size_t i = 0; i < size; i += 3) {
    const std::uint32_t b0 = data[i];
    const std::uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
    const std::uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
    const std::uint32_t triple = (b0 << 16u) | (b1 << 8u) | b2;
    out.push_back(kAlphabet[(triple >> 18u) & 0x3fu]);
    out.push_back(kAlphabet[(triple >> 12u) & 0x3fu]);
    out.push_back(i + 1 < size ? kAlphabet[(triple >> 6u) & 0x3fu] : '=');
    out.push_back(i + 2 < size ? kAlphabet[triple & 0x3fu] : '=');
  }
  return out;
}

[[nodiscard]] std::string websocketAcceptKey(const std::string &clientKey) {
  static constexpr std::string_view kGuid =
      "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const std::string source = clientKey + std::string(kGuid);
  const auto digest = sha1(source);
  return base64Encode(digest.data(), digest.size());
}

struct ParsedHttpRequest final {
  std::string method;
  std::string path;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct WsFrame final {
  bool fin = true;
  std::uint8_t opcode = 0;
  std::string payload;
};

[[nodiscard]] std::optional<ParsedHttpRequest>
tryParseHttpRequest(const std::string &buffer, size_t &consumedBytes) {
  const size_t headerEnd = buffer.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    return std::nullopt;
  }

  std::istringstream stream(buffer.substr(0, headerEnd));
  ParsedHttpRequest request;
  std::string requestLine;
  if (!std::getline(stream, requestLine)) {
    return std::nullopt;
  }
  requestLine = trim(requestLine);
  std::istringstream requestLineStream(requestLine);
  requestLineStream >> request.method >> request.path;

  std::string line;
  while (std::getline(stream, line)) {
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    request.headers.emplace(lowerCopy(trim(line.substr(0, colon))),
                            trim(line.substr(colon + 1)));
  }

  size_t contentLength = 0;
  if (const auto contentLengthText =
          headerValue(request.headers, "content-length");
      contentLengthText.has_value()) {
    contentLength = static_cast<size_t>(std::stoul(*contentLengthText));
  }
  const size_t totalSize = headerEnd + 4 + contentLength;
  if (buffer.size() < totalSize) {
    return std::nullopt;
  }
  request.body = buffer.substr(headerEnd + 4, contentLength);
  consumedBytes = totalSize;
  return request;
}

[[nodiscard]] std::optional<WsFrame> tryParseWsFrame(const std::string &buffer,
                                                     size_t &consumedBytes) {
  if (buffer.size() < 2) {
    return std::nullopt;
  }
  const std::uint8_t b0 = static_cast<std::uint8_t>(buffer[0]);
  const std::uint8_t b1 = static_cast<std::uint8_t>(buffer[1]);
  const bool masked = (b1 & 0x80u) != 0u;
  std::uint64_t payloadLength = b1 & 0x7fu;
  size_t offset = 2;
  if (payloadLength == 126u) {
    if (buffer.size() < offset + 2) {
      return std::nullopt;
    }
    payloadLength = (static_cast<std::uint8_t>(buffer[offset]) << 8u) |
                    static_cast<std::uint8_t>(buffer[offset + 1]);
    offset += 2;
  } else if (payloadLength == 127u) {
    if (buffer.size() < offset + 8) {
      return std::nullopt;
    }
    payloadLength = 0;
    for (int i = 0; i < 8; ++i) {
      payloadLength =
          (payloadLength << 8u) | static_cast<std::uint8_t>(buffer[offset + i]);
    }
    offset += 8;
  }

  std::array<std::uint8_t, 4> mask{};
  if (masked) {
    if (buffer.size() < offset + 4) {
      return std::nullopt;
    }
    for (int i = 0; i < 4; ++i) {
      mask[i] = static_cast<std::uint8_t>(buffer[offset + i]);
    }
    offset += 4;
  }
  if (buffer.size() < offset + payloadLength) {
    return std::nullopt;
  }

  WsFrame frame;
  frame.fin = (b0 & 0x80u) != 0u;
  frame.opcode = b0 & 0x0fu;
  frame.payload.resize(static_cast<size_t>(payloadLength));
  for (size_t i = 0; i < payloadLength; ++i) {
    char value = buffer[offset + i];
    if (masked) {
      value = static_cast<char>(static_cast<std::uint8_t>(value) ^ mask[i % 4]);
    }
    frame.payload[i] = value;
  }
  consumedBytes = offset + static_cast<size_t>(payloadLength);
  return frame;
}

[[nodiscard]] std::string makeWsTextFrame(const std::string &payload,
                                          const std::uint8_t opcode = 0x1u) {
  std::string out;
  out.push_back(static_cast<char>(0x80u | opcode));
  if (payload.size() < 126u) {
    out.push_back(static_cast<char>(payload.size()));
  } else if (payload.size() <= 0xffffu) {
    out.push_back(static_cast<char>(126u));
    out.push_back(static_cast<char>((payload.size() >> 8u) & 0xffu));
    out.push_back(static_cast<char>(payload.size() & 0xffu));
  } else {
    out.push_back(static_cast<char>(127u));
    for (int shift = 56; shift >= 0; shift -= 8) {
      out.push_back(static_cast<char>((payload.size() >> shift) & 0xffu));
    }
  }
  out += payload;
  return out;
}

[[nodiscard]] std::optional<std::string> bearerTokenFromHeaders(
    const std::unordered_map<std::string, std::string> &headers) {
  const auto auth = headerValue(headers, "authorization");
  if (!auth.has_value()) {
    return std::nullopt;
  }
  static constexpr std::string_view kPrefix = "Bearer ";
  if (auth->rfind(kPrefix, 0) != 0) {
    return std::nullopt;
  }
  return auth->substr(kPrefix.size());
}

[[nodiscard]] std::optional<std::string>
queryTokenFromPath(const std::string &path) {
  const size_t queryPos = path.find('?');
  if (queryPos == std::string::npos) {
    return std::nullopt;
  }
  const std::string query = path.substr(queryPos + 1);
  size_t begin = 0;
  while (begin < query.size()) {
    const size_t end = query.find('&', begin);
    const std::string pair = query.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    const size_t equals = pair.find('=');
    if (equals != std::string::npos && pair.substr(0, equals) == "token") {
      return pair.substr(equals + 1);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
queryParamFromPath(const std::string &path, const std::string &name) {
  const size_t queryPos = path.find('?');
  if (queryPos == std::string::npos) {
    return std::nullopt;
  }
  const std::string query = path.substr(queryPos + 1);
  size_t begin = 0;
  while (begin < query.size()) {
    const size_t end = query.find('&', begin);
    const std::string pair = query.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    const size_t equals = pair.find('=');
    if (equals != std::string::npos && pair.substr(0, equals) == name) {
      return pair.substr(equals + 1);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return std::nullopt;
}

[[nodiscard]] int hexValue(const char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + c - 'a';
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + c - 'A';
  }
  return -1;
}

[[nodiscard]] std::string urlDecode(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '+') {
      out.push_back(' ');
      continue;
    }
    if (text[i] == '%' && i + 2 < text.size()) {
      const int hi = hexValue(text[i + 1]);
      const int lo = hexValue(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

[[nodiscard]] bool isAuthorized(const ParsedHttpRequest &request,
                                const std::string &expectedToken) {
  const auto bearer = bearerTokenFromHeaders(request.headers);
  if (bearer.has_value() && *bearer == expectedToken) {
    return true;
  }
  const auto queryToken = queryTokenFromPath(request.path);
  return queryToken.has_value() && *queryToken == expectedToken;
}

} // namespace

struct LxeEditorApiServer::Impl final {
  struct Client final {
    enum class Kind {
      Http,
      WebSocket,
    };

    SocketHandle socket = kInvalidSocket;
    Kind kind = Kind::Http;
    bool closeAfterWrite = true;
    std::string readBuffer;
    std::string writeBuffer;
    ApiEventCursor cursor;
  };

  SocketHandle listenSocket = kInvalidSocket;
  std::vector<Client> clients;
  bool running = false;
};

LxeEditorApiServer::LxeEditorApiServer(LxeEditorApiServerConfig config)
    : m_config(std::move(config)), m_impl(new Impl()) {}

LxeEditorApiServer::~LxeEditorApiServer() {
  stop();
  delete m_impl;
}

bool LxeEditorApiServer::start(std::string *errorMessage) {
  if (!m_config.enabled) {
    return true;
  }
  if (m_impl->running) {
    return true;
  }
#if defined(_WIN32)
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    if (errorMessage) {
      *errorMessage = "WSAStartup failed";
    }
    return false;
  }
#endif

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo *result = nullptr;
  const std::string portText = std::to_string(m_config.port);
  const int getaddrinfoResult =
      getaddrinfo(m_config.host.c_str(), portText.c_str(), &hints, &result);
  if (getaddrinfoResult != 0 || !result) {
    if (errorMessage) {
      *errorMessage = "getaddrinfo failed";
    }
    return false;
  }

  SocketHandle listenSocket = kInvalidSocket;
  for (addrinfo *addr = result; addr != nullptr; addr = addr->ai_next) {
    listenSocket =
        socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (listenSocket == kInvalidSocket) {
      continue;
    }
    const int one = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&one), sizeof(one));
    if (bind(listenSocket, addr->ai_addr, addr->ai_addrlen) == 0 &&
        listen(listenSocket, 16) == 0 && setNonBlocking(listenSocket)) {
      break;
    }
    closeSocket(listenSocket);
    listenSocket = kInvalidSocket;
  }
  freeaddrinfo(result);

  if (listenSocket == kInvalidSocket) {
    if (errorMessage) {
      *errorMessage =
          "failed to bind/listen API server: " + socketErrorString();
    }
    return false;
  }

  sockaddr_in boundAddr{};
  socklen_t boundAddrSize = sizeof(boundAddr);
  if (getsockname(listenSocket, reinterpret_cast<sockaddr *>(&boundAddr),
                  &boundAddrSize) == 0) {
    m_config.port = ntohs(boundAddr.sin_port);
  }
  m_impl->listenSocket = listenSocket;
  m_impl->running = true;
  return true;
}

void LxeEditorApiServer::stop() {
  if (!m_impl) {
    return;
  }
  for (auto &client : m_impl->clients) {
    closeSocket(client.socket);
    client.socket = kInvalidSocket;
  }
  m_impl->clients.clear();
  closeSocket(m_impl->listenSocket);
  m_impl->listenSocket = kInvalidSocket;
  m_impl->running = false;
#if defined(_WIN32)
  WSACleanup();
#endif
}

bool LxeEditorApiServer::isRunning() const { return m_impl && m_impl->running; }

const LxeEditorApiServerConfig &LxeEditorApiServer::config() const {
  return m_config;
}

std::uint16_t LxeEditorApiServer::boundPort() const { return m_config.port; }

void LxeEditorApiServer::pump(LxeEditorApiService &service) {
  if (!m_impl->running) {
    return;
  }

  while (true) {
    sockaddr_in clientAddr{};
    socklen_t clientAddrSize = sizeof(clientAddr);
    const SocketHandle clientSocket =
        accept(m_impl->listenSocket, reinterpret_cast<sockaddr *>(&clientAddr),
               &clientAddrSize);
    if (clientSocket == kInvalidSocket) {
      break;
    }
    if (!setNonBlocking(clientSocket)) {
      closeSocket(clientSocket);
      continue;
    }
    m_impl->clients.push_back(Impl::Client{.socket = clientSocket});
  }

  for (auto &client : m_impl->clients) {
    char buffer[4096];
    while (true) {
      const int bytesRead =
          recv(client.socket, buffer, static_cast<int>(sizeof(buffer)), 0);
      if (bytesRead > 0) {
        client.readBuffer.append(buffer, buffer + bytesRead);
        continue;
      }
      if (bytesRead == 0) {
        closeSocket(client.socket);
        client.socket = kInvalidSocket;
      } else if (!wouldBlock()) {
        closeSocket(client.socket);
        client.socket = kInvalidSocket;
      }
      break;
    }
  }

  for (auto &client : m_impl->clients) {
    if (client.kind == Impl::Client::Kind::Http) {
      size_t consumed = 0;
      const auto request = tryParseHttpRequest(client.readBuffer, consumed);
      if (!request.has_value()) {
        continue;
      }
      client.readBuffer.erase(0, consumed);

      const std::string pathWithoutQuery =
          request->path.substr(0, request->path.find('?'));
      if (pathWithoutQuery != "/health" &&
          !isAuthorized(*request, m_config.token)) {
        client.writeBuffer +=
            httpResponse("401 Unauthorized",
                         "{\"ok\":false,\"error\":{\"code\":\"unauthorized\","
                         "\"message\":\"missing or invalid token\"}}");
        client.closeAfterWrite = true;
        continue;
      }

      if (pathWithoutQuery == "/health") {
        client.writeBuffer +=
            httpResponse("200 OK", "{\"ok\":true,\"status\":\"ok\"}");
        client.closeAfterWrite = true;
        continue;
      }

      const auto upgrade = headerValue(request->headers, "upgrade");
      if (upgrade.has_value() && lowerCopy(*upgrade) == "websocket" &&
          pathWithoutQuery == "/ws") {
        const auto key = headerValue(request->headers, "sec-websocket-key");
        if (!key.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing websocket key\"}}");
          client.closeAfterWrite = true;
          continue;
        }
        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: websocket\r\n"
                 << "Connection: Upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << websocketAcceptKey(*key)
                 << "\r\n\r\n";
        client.writeBuffer += response.str();
        client.closeAfterWrite = false;
        client.kind = Impl::Client::Kind::WebSocket;
        client.cursor = service.currentCursor();
        continue;
      }

      auto commandResponse = [&](const std::string &line) {
        return toJson(service.executeCommand(ApiCommandRequest{.line = line}));
      };

      if (request->method == "POST" && pathWithoutQuery == "/api/command") {
        const auto line = jsonStringField(request->body, "line");
        if (!line.has_value()) {
          client.writeBuffer += httpResponse(
              "400 Bad Request", "{\"ok\":false,\"error\":{\"code\":\"bad_"
                                 "request\",\"message\":\"missing line\"}}");
        } else {
          client.writeBuffer += httpResponse("200 OK", commandResponse(*line));
        }
      } else if (request->method == "GET" && pathWithoutQuery == "/api/state") {
        client.writeBuffer +=
            httpResponse("200 OK", toJson(service.captureState()));
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/state/summary") {
        client.writeBuffer +=
            httpResponse("200 OK", summaryJson(service.captureState()));
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/state/selection") {
        client.writeBuffer +=
            httpResponse("200 OK", toJson(service.captureState().selection));
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/state/cameras") {
        client.writeBuffer +=
            httpResponse("200 OK", toJson(service.captureState().cameras));
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/state/scene") {
        client.writeBuffer +=
            httpResponse("200 OK", toJson(service.captureState().scene));
      } else if (request->method == "GET" && pathWithoutQuery == "/api/build") {
        client.writeBuffer += httpResponse("200 OK", service.buildInfo());
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/display/list") {
        client.writeBuffer += httpResponse("200 OK", service.displayList());
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/display/active") {
        client.writeBuffer += httpResponse("200 OK", service.displayActive());
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/display/config") {
        const auto key = queryParamFromPath(request->path, "key");
        if (!key.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing display config key\"}}");
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", service.displayConfigGet(urlDecode(*key)));
        }
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/api/display/config") {
        const auto key = jsonStringField(request->body, "key");
        const auto patch =
            jsonStringOrRawContainerField(request->body, "patch");
        if (!key.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing display config key\"}}");
        } else if (!patch.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing display config patch\"}}");
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", service.displayConfigSet(*key, *patch));
        }
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/api/display/select") {
        const auto key = jsonStringField(request->body, "key");
        if (!key.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing display key\"}}");
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", service.displaySelect(*key));
        }
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/api/state/toolbar") {
        client.writeBuffer +=
            httpResponse("200 OK", toJson(service.captureState().toolbar));
      } else if (request->method == "POST" && pathWithoutQuery == "/api/mode") {
        const auto mode = jsonStringField(request->body, "mode");
        if (!mode.has_value()) {
          client.writeBuffer += httpResponse(
              "400 Bad Request", "{\"ok\":false,\"error\":{\"code\":\"bad_"
                                 "request\",\"message\":\"missing mode\"}}");
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", commandResponse("mode " + *mode));
        }
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/api/preview") {
        if (const auto enabled = jsonBoolField(request->body, "enabled");
            enabled.has_value()) {
          client.writeBuffer += httpResponse(
              "200 OK", commandResponse(std::string("preview ") +
                                        (*enabled ? "on" : "off")));
        } else if (const auto action = jsonStringField(request->body, "action");
                   action.has_value()) {
          client.writeBuffer +=
              httpResponse("200 OK", commandResponse("preview " + *action));
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", commandResponse("preview toggle"));
        }
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/api/camera/reset-editor-to-game") {
        client.writeBuffer +=
            httpResponse("200 OK", commandResponse("cam reset-editor-to-game"));
      } else if (request->method == "POST" && pathWithoutQuery == "/api/pick") {
        const auto x = jsonNumberField(request->body, "x");
        const auto y = jsonNumberField(request->body, "y");
        if (!x.has_value() || !y.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing pick coordinates\"}}");
        } else {
          std::ostringstream line;
          line << "pick " << *x << " " << *y;
          client.writeBuffer +=
              httpResponse("200 OK", commandResponse(line.str()));
        }
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/recording/status") {
        client.writeBuffer += httpResponse("200 OK", service.recordingStatus());
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/recording/enable") {
        client.writeBuffer += httpResponse("200 OK", service.recordingEnable());
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/recording/disable") {
        client.writeBuffer += httpResponse(
            "200 OK",
            service.recordingDisable(
                jsonBoolField(request->body, "force").value_or(false)));
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/recording/start") {
        RecordingDetailLevel detailLevel = RecordingDetailLevel::Basic;
        if (const auto detail = jsonStringField(request->body, "detailLevel");
            detail.has_value()) {
          detailLevel = recordingDetailLevelFromName(*detail).value_or(
              RecordingDetailLevel::Basic);
        }
        client.writeBuffer +=
            httpResponse("200 OK", service.recordingStart(detailLevel));
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/recording/stop") {
        client.writeBuffer += httpResponse(
            "200 OK", service.recordingStop(
                          jsonBoolField(request->body, "save").value_or(true)));
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/recording/list") {
        client.writeBuffer += httpResponse("200 OK", service.recordingList());
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/recording/read") {
        const auto id = queryParamFromPath(request->path, "id");
        if (!id.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing recording id\"}}");
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", service.recordingRead(urlDecode(*id)));
        }
      } else if (request->method == "POST" &&
                 pathWithoutQuery == "/recording/replay") {
        std::optional<std::string> id = jsonStringField(request->body, "id");
        if (!id.has_value()) {
          id = jsonStringField(request->body, "path");
        }
        if (!id.has_value()) {
          client.writeBuffer +=
              httpResponse("400 Bad Request",
                           "{\"ok\":false,\"error\":{\"code\":\"bad_request\","
                           "\"message\":\"missing recording id\"}}");
        } else {
          client.writeBuffer +=
              httpResponse("200 OK", service.recordingReplay(urlDecode(*id)));
        }
      } else if (request->method == "GET" &&
                 pathWithoutQuery == "/recording/probe") {
        client.writeBuffer += httpResponse(
            "200 OK",
            service.recordingProbe(
                queryParamFromPath(request->path, "target").value_or("state")));
      } else {
        client.writeBuffer += httpResponse(
            "404 Not Found", "{\"ok\":false,\"error\":{\"code\":\"not_found\","
                             "\"message\":\"unknown endpoint\"}}");
      }
      client.closeAfterWrite = true;
      continue;
    }

    while (client.kind == Impl::Client::Kind::WebSocket) {
      size_t consumed = 0;
      const auto frame = tryParseWsFrame(client.readBuffer, consumed);
      if (!frame.has_value()) {
        break;
      }
      client.readBuffer.erase(0, consumed);
      if (frame->opcode == 0x8u) {
        client.writeBuffer += makeWsTextFrame({}, 0x8u);
        client.closeAfterWrite = true;
        break;
      }
      if (frame->opcode == 0x9u) {
        client.writeBuffer += makeWsTextFrame(frame->payload, 0xAu);
        continue;
      }
      if (frame->opcode != 0x1u) {
        continue;
      }
      const auto type = jsonStringField(frame->payload, "type");
      if (type.has_value() && *type == "command") {
        const auto line = jsonStringField(frame->payload, "line");
        if (!line.has_value()) {
          client.writeBuffer += makeWsTextFrame(
              "{\"type\":\"command.response\",\"ok\":false,\"error\":{\"code\":"
              "\"bad_request\",\"message\":\"missing line\"}}");
        } else {
          const auto response =
              service.executeCommand(ApiCommandRequest{.line = *line});
          client.writeBuffer += makeWsTextFrame(
              std::string("{\"type\":\"command.response\",\"payload\":") +
              toJson(response) + "}");
        }
      }
    }
  }

  for (auto &client : m_impl->clients) {
    if (client.kind != Impl::Client::Kind::WebSocket) {
      continue;
    }
    const ApiEventBatch batch = service.collectEventsSince(client.cursor);
    client.cursor = batch.nextCursor;
    for (const auto &event : batch.events) {
      client.writeBuffer += makeWsTextFrame(toJson(event));
    }
  }

  for (auto &client : m_impl->clients) {
    while (!client.writeBuffer.empty()) {
      const int sent = send(client.socket, client.writeBuffer.data(),
                            static_cast<int>(client.writeBuffer.size()), 0);
      if (sent <= 0) {
        if (!wouldBlock()) {
          closeSocket(client.socket);
          client.socket = kInvalidSocket;
        }
        break;
      }
      client.writeBuffer.erase(0, static_cast<size_t>(sent));
    }
  }

  auto eraseBegin =
      std::remove_if(m_impl->clients.begin(), m_impl->clients.end(),
                     [](const Impl::Client &client) {
                       const bool deadSocket = client.socket == kInvalidSocket;
                       const bool readyToClose =
                           client.closeAfterWrite && client.writeBuffer.empty();
                       return deadSocket || readyToClose;
                     });
  for (auto it = eraseBegin; it != m_impl->clients.end(); ++it) {
    closeSocket(it->socket);
  }
  m_impl->clients.erase(eraseBegin, m_impl->clients.end());
}

} // namespace LX_demo::lxe_editor
