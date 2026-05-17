#include "core/editor/commands/builtin_commands.hpp"

#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/components/material_component.hpp"
#include "core/scene/components/mesh_component.hpp"
#include "core/scene/components/skeleton_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace LX_core {
namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kDuplicateOffsetX = 0.5f;

struct CameraClipboardState final {
  CameraType type = CameraType::Perspective;
  float fovY = 45.0f;
  float aspect = 16.0f / 9.0f;
  float nearPlane = 0.1f;
  float farPlane = 1000.0f;
  float left = -1.0f;
  float right = 1.0f;
  float bottom = -1.0f;
  float top = 1.0f;
  VisibilityLayerMask cullingMask = Layer_All & ~Layer_EditorOverlay;
  bool active = true;
  std::optional<RenderTarget> target;
};

struct DirectionalLightClipboardState final {
  Vec3f direction{-0.3f, -1.0f, -0.5f};
  Vec3f color{1.0f, 0.98f, 0.9f};
  float intensity = 1.0f;
  float shadowStrength = 0.45f;
  float shadowDistance = 80.0f;
  u32 shadowCascadeCount = MaxShadowCascades;
};

struct NodeClipboardEntry final {
  std::string nodeName;
  std::string name;
  Transform transform = Transform::identity();
  VisibilityLayerMask visibilityMask = VisibilityMask_All;
  MeshSharedPtr mesh;
  MaterialInstanceSharedPtr material;
  SkeletonSharedPtr skeleton;
  std::optional<CameraClipboardState> camera;
  std::optional<DirectionalLightClipboardState> directionalLight;
  std::vector<NodeClipboardEntry> children;
};

[[nodiscard]] std::string jsonEscape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string formatFloat(const float value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3) << value;
  return oss.str();
}

[[nodiscard]] CommandResult makeError(std::string message) {
  return CommandResult{false, std::move(message), {}};
}

[[nodiscard]] CommandResult makeOk(std::string message,
                                   std::string structured = {}) {
  return CommandResult{true, std::move(message), std::move(structured)};
}

[[nodiscard]] CommandResult makeSceneIoUnavailable(const std::string &action) {
  return makeError("scene I/O unavailable: scene " + action +
                   " callback is not registered");
}

[[nodiscard]] CommandResult makeAdminUnavailable(const std::string &action) {
  return makeError("admin unavailable: admin " + action +
                   " callback is not registered");
}

[[nodiscard]] CommandResult markClearsHistoryOnSuccess(CommandResult result) {
  if (result.ok) {
    result.metadata[std::string(kCommandResultClearUndoOnSuccessMetadataKey)] =
        "true";
    result.metadata[std::string(kCommandResultClearRedoOnSuccessMetadataKey)] =
        "true";
  }
  return result;
}

[[nodiscard]] std::optional<float> parseFloat(const std::string &text) {
  try {
    size_t index = 0;
    const float value = std::stof(text, &index);
    if (index != text.size()) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<u32> parseUnsigned(const std::string &text) {
  try {
    size_t index = 0;
    const unsigned long value = std::stoul(text, &index, 0);
    if (index != text.size() || value > std::numeric_limits<u32>::max()) {
      return std::nullopt;
    }
    return static_cast<u32>(value);
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<i32> parseInt(const std::string &text) {
  try {
    size_t index = 0;
    const long value = std::stol(text, &index, 0);
    if (index != text.size() || value < std::numeric_limits<i32>::min() ||
        value > std::numeric_limits<i32>::max()) {
      return std::nullopt;
    }
    return static_cast<i32>(value);
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<Vec3f>
parseVec3(const std::vector<std::string> &args, const usize startIndex) {
  if (startIndex + 2 >= args.size()) {
    return std::nullopt;
  }
  const auto x = parseFloat(args[startIndex]);
  const auto y = parseFloat(args[startIndex + 1]);
  const auto z = parseFloat(args[startIndex + 2]);
  if (!x || !y || !z) {
    return std::nullopt;
  }
  return Vec3f{*x, *y, *z};
}

[[nodiscard]] std::optional<Vec4f>
parseVec4(const std::vector<std::string> &args, const usize startIndex) {
  if (startIndex + 3 >= args.size()) {
    return std::nullopt;
  }
  const auto x = parseFloat(args[startIndex]);
  const auto y = parseFloat(args[startIndex + 1]);
  const auto z = parseFloat(args[startIndex + 2]);
  const auto w = parseFloat(args[startIndex + 3]);
  if (!x || !y || !z || !w) {
    return std::nullopt;
  }
  return Vec4f{*x, *y, *z, *w};
}

[[nodiscard]] std::string quoteToken(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  out.push_back('"');
  return out;
}

[[nodiscard]] CommandResult requireNode(Scene &scene, const std::string &path,
                                        SceneNode *&outNode) {
  SceneNode *node = scene.findByPath(path);
  if (!node) {
    return makeError("node not found: " + path);
  }
  outNode = node;
  return CommandResult{true, {}, {}};
}

[[nodiscard]] bool siblingNameExists(const SceneNode &node,
                                     const std::string &name) {
  if (name.empty()) {
    return false;
  }

  const auto parent = node.getParent();
  const std::vector<SceneNodeSharedPtr> siblings =
      parent
          ? parent->getChildren()
          : (node.getAttachedScene() ? node.getAttachedScene()->getRootNodes()
                                     : std::vector<SceneNodeSharedPtr>{});
  for (const auto &sibling : siblings) {
    if (sibling && sibling.get() != &node && sibling->getName() == name) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool childNameExists(const SceneNodeSharedPtr &parent,
                                   const std::string &name) {
  if (!parent || name.empty()) {
    return false;
  }
  for (const auto &child : parent->getChildren()) {
    if (child && child->getName() == name) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string stripCopySuffix(const std::string &name) {
  const std::string suffix = ".copy";
  const auto suffixPos = name.rfind(suffix);
  if (suffixPos == std::string::npos) {
    return name;
  }
  const usize afterSuffix = suffixPos + suffix.size();
  if (afterSuffix == name.size()) {
    return name.substr(0, suffixPos);
  }
  if (afterSuffix + 4 == name.size() && name[afterSuffix] == '.' &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 1])) &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 2])) &&
      std::isdigit(static_cast<unsigned char>(name[afterSuffix + 3]))) {
    return name.substr(0, suffixPos);
  }
  return name;
}

[[nodiscard]] std::string makeCopyName(const SceneNodeSharedPtr &parent,
                                       const std::string &sourceName) {
  const std::string base =
      stripCopySuffix(sourceName.empty() ? "node" : sourceName);
  std::string candidate = base + ".copy";
  if (!childNameExists(parent, candidate)) {
    return candidate;
  }

  for (u32 index = 1; index < 100000; ++index) {
    std::ostringstream suffix;
    suffix << ".copy." << std::setw(3) << std::setfill('0') << index;
    candidate = base + suffix.str();
    if (!childNameExists(parent, candidate)) {
      return candidate;
    }
  }
  return base + ".copy.overflow";
}

[[nodiscard]] std::string joinLines(const std::vector<std::string> &lines) {
  std::ostringstream oss;
  for (usize i = 0; i < lines.size(); ++i) {
    if (i != 0) {
      oss << '\n';
    }
    oss << lines[i];
  }
  return oss.str();
}

[[nodiscard]] std::string
buildSelectionCommand(const std::vector<SceneNodeSharedPtr> &selection) {
  if (selection.empty()) {
    return "deselect";
  }

  std::ostringstream oss;
  oss << "select";
  for (const auto &node : selection) {
    if (node) {
      oss << ' ' << quoteToken(node->getPath());
    }
  }
  return oss.str();
}

[[nodiscard]] std::optional<std::vector<SceneNodeSharedPtr>>
resolveNodePaths(Scene &scene, const std::vector<std::string> &paths,
                 CommandResult &error) {
  std::vector<SceneNodeSharedPtr> nodes;
  nodes.reserve(paths.size());
  for (const auto &path : paths) {
    SceneNode *node = nullptr;
    const CommandResult found = requireNode(scene, path, node);
    if (!found.ok) {
      error = found;
      return std::nullopt;
    }
    nodes.push_back(node->shared_from_this());
  }
  return nodes;
}

[[nodiscard]] std::optional<std::vector<std::string>>
extractTargetPathsAndVec3Args(const std::vector<std::string> &args,
                              CommandResult &error,
                              const std::string &usageText,
                              const std::string &invalidFloatText) {
  if (args.size() < 4) {
    error = makeError(usageText);
    return std::nullopt;
  }

  const auto value = parseVec3(args, args.size() - 3);
  if (!value) {
    error = makeError(invalidFloatText);
    return std::nullopt;
  }

  std::vector<std::string> paths;
  for (usize i = 0; i + 3 < args.size(); ++i) {
    paths.push_back(args[i]);
  }
  if (paths.empty()) {
    error = makeError(usageText);
    return std::nullopt;
  }
  return paths;
}

[[nodiscard]] std::optional<std::vector<std::string>>
extractTargetPathsAndScaleArgs(const std::vector<std::string> &args,
                               CommandResult &error) {
  if (args.size() < 2) {
    error = makeError("usage: scale <path> <s> | scale <path> <sx> <sy> <sz>");
    return std::nullopt;
  }

  usize numericCount = 0;
  if (args.size() >= 4 && parseVec3(args, args.size() - 3).has_value()) {
    numericCount = 3;
  } else if (parseFloat(args.back()).has_value()) {
    numericCount = 1;
  } else {
    error = makeError("invalid float for scale");
    return std::nullopt;
  }

  if (args.size() <= numericCount) {
    error = makeError("usage: scale <path> <s> | scale <path> <sx> <sy> <sz>");
    return std::nullopt;
  }

  std::vector<std::string> paths;
  for (usize i = 0; i + numericCount < args.size(); ++i) {
    paths.push_back(args[i]);
  }
  if (numericCount == 1 && paths.size() != 1) {
    error = makeError("usage: scale <path> <s> | scale <path> <sx> <sy> <sz>");
    return std::nullopt;
  }
  if (paths.empty()) {
    error = makeError("usage: scale <path> <s> | scale <path> <sx> <sy> <sz>");
    return std::nullopt;
  }
  return paths;
}

[[nodiscard]] SceneNodeSharedPtr chooseCommandParent(EditorState &editorState) {
  const auto selected = editorState.getSelected();
  if (selected.empty()) {
    return {};
  }
  return selected.back();
}

[[nodiscard]] std::string
buildHelpMessage(CommandBus &bus, const std::vector<std::string> &args) {
  if (args.empty()) {
    std::ostringstream oss;
    const auto verbs = bus.listVerbs();
    for (usize i = 0; i < verbs.size(); ++i) {
      const auto &verb = verbs[i];
      oss << verb;
      const std::string summary = bus.brief(verb);
      if (!summary.empty()) {
        oss << " - " << summary;
      }
      if (i + 1 != verbs.size()) {
        oss << '\n';
      }
    }
    return oss.str();
  }

  if (args.size() != 1) {
    return {};
  }

  const std::string summary = bus.brief(args.front());
  if (summary.empty()) {
    return "unknown command: " + args.front();
  }
  return args.front() + " - " + summary;
}

[[nodiscard]] std::string
makeVerbListJson(const std::vector<std::string> &verbs) {
  std::ostringstream oss;
  oss << "{\"verbs\":[";
  for (usize i = 0; i < verbs.size(); ++i) {
    if (i != 0) {
      oss << ',';
    }
    oss << '"' << jsonEscape(verbs[i]) << '"';
  }
  oss << "]}";
  return oss.str();
}

[[nodiscard]] Quatf eulerDegreesToQuat(const float rxDeg, const float ryDeg,
                                       const float rzDeg) {
  const Quatf qx =
      Quatf::fromAxisAngle(Vec3f{1.0f, 0.0f, 0.0f}, rxDeg * kDegToRad);
  const Quatf qy =
      Quatf::fromAxisAngle(Vec3f{0.0f, 1.0f, 0.0f}, ryDeg * kDegToRad);
  const Quatf qz =
      Quatf::fromAxisAngle(Vec3f{0.0f, 0.0f, 1.0f}, rzDeg * kDegToRad);
  return (qz * qy * qx).normalized();
}

[[nodiscard]] Vec3f quatToEulerDegrees(const Quatf &quat) {
  const Quatf q = quat.normalized();

  const float sinrCosp = 2.0f * (q.w * q.v.x + q.v.y * q.v.z);
  const float cosrCosp = 1.0f - 2.0f * (q.v.x * q.v.x + q.v.y * q.v.y);
  const float roll = std::atan2(sinrCosp, cosrCosp);

  const float sinp = 2.0f * (q.w * q.v.y - q.v.z * q.v.x);
  const float pitch = std::abs(sinp) >= 1.0f
                          ? std::copysign(0.5f * 3.14159265358979323846f, sinp)
                          : std::asin(sinp);

  const float sinyCosp = 2.0f * (q.w * q.v.z + q.v.x * q.v.y);
  const float cosyCosp = 1.0f - 2.0f * (q.v.y * q.v.y + q.v.z * q.v.z);
  const float yaw = std::atan2(sinyCosp, cosyCosp);

  constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
  return Vec3f{roll * kRadToDeg, pitch * kRadToDeg, yaw * kRadToDeg};
}

[[nodiscard]] std::string makeVec3Json(const Vec3f &value) {
  std::ostringstream oss;
  oss << "{\"x\":" << value.x << ",\"y\":" << value.y << ",\"z\":" << value.z
      << "}";
  return oss.str();
}

[[nodiscard]] std::string makeQuatJson(const Quatf &value) {
  std::ostringstream oss;
  oss << "{\"w\":" << value.w << ",\"x\":" << value.v.x
      << ",\"y\":" << value.v.y << ",\"z\":" << value.v.z << "}";
  return oss.str();
}

[[nodiscard]] std::string makeUnsignedJson(const u32 value) {
  return "{\"value\":" + std::to_string(value) + "}";
}

[[nodiscard]] const char *
materialParameterTypeName(const MaterialParameterValueType type) {
  switch (type) {
  case MaterialParameterValueType::Float:
    return "float";
  case MaterialParameterValueType::Int:
    return "int";
  case MaterialParameterValueType::Vec3:
    return "Vec3";
  case MaterialParameterValueType::Vec4:
    return "Vec4";
  }
  return "unknown";
}

[[nodiscard]] std::string
makeMaterialValueJson(const MaterialParameterValue &value) {
  switch (value.type) {
  case MaterialParameterValueType::Float:
    return std::to_string(value.floatValue);
  case MaterialParameterValueType::Int:
    return std::to_string(value.intValue);
  case MaterialParameterValueType::Vec3:
    return "[" + std::to_string(value.vectorValue.x) + "," +
           std::to_string(value.vectorValue.y) + "," +
           std::to_string(value.vectorValue.z) + "]";
  case MaterialParameterValueType::Vec4:
    return "[" + std::to_string(value.vectorValue.x) + "," +
           std::to_string(value.vectorValue.y) + "," +
           std::to_string(value.vectorValue.z) + "," +
           std::to_string(value.vectorValue.w) + "]";
  }
  return "null";
}

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::transform(
      text.begin(), text.end(), text.begin(),
      [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

[[nodiscard]] std::shared_ptr<DirectionalLight>
resolveDirectionalLight(SceneNode &node) {
  const auto scene = node.getAttachedScene();
  if (!scene) {
    return nullptr;
  }
  return scene->getDirectionalLight(node);
}

[[nodiscard]] LightBaseSharedPtr resolveLight(SceneNode &node) {
  const auto scene = node.getAttachedScene();
  if (!scene) {
    return nullptr;
  }
  return scene->getLight(node);
}

[[nodiscard]] std::string lightKindName(const LightBaseSharedPtr &light) {
  if (std::dynamic_pointer_cast<DirectionalLight>(light)) {
    return "Directional";
  }
  if (std::dynamic_pointer_cast<PointLight>(light)) {
    return "Point";
  }
  if (std::dynamic_pointer_cast<SpotLight>(light)) {
    return "Spot";
  }
  return {};
}

void setLightCastsShadow(const LightBaseSharedPtr &light,
                         const bool castsShadow) {
  std::vector<StringID> passes{Pass_Forward, Pass_Deferred};
  if (castsShadow) {
    passes.push_back(Pass_Shadow);
  }
  if (const auto directional =
          std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directional->setSupportedPasses(passes);
  } else if (const auto point = std::dynamic_pointer_cast<PointLight>(light)) {
    point->setSupportedPasses(passes);
  } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
    spot->setSupportedPasses(passes);
  }
}

[[nodiscard]] Vec3f lightForwardDirection(const SceneNode &node,
                                          const Vec3f &fallback) {
  const Transform world = Transform::fromMat4(node.getWorldTransform());
  Vec3f direction = world.rotation.rotate(Vec3f{0.0f, 0.0f, -1.0f});
  if (direction.length2() <= 1e-6f) {
    direction = fallback;
  }
  if (direction.length2() <= 1e-6f) {
    return Vec3f{0.0f, 0.0f, -1.0f};
  }
  return direction.normalized();
}

bool syncLightSpatialProperties(Scene &scene, const SceneNodeSharedPtr &node) {
  if (!node) {
    return false;
  }
  const auto light = scene.getLight(*node);
  if (!light) {
    return false;
  }
  if (const auto directional =
          std::dynamic_pointer_cast<DirectionalLight>(light)) {
    directional->setDirection(
        lightForwardDirection(*node, directional->getDirection()));
  } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
    spot->setDirection(lightForwardDirection(*node, spot->getDirection()));
  }
  return true;
}

[[nodiscard]] std::optional<bool> parseBoolToken(const std::string &text) {
  const std::string value = lowerCopy(text);
  if (value == "true" || value == "1" || value == "on" || value == "yes") {
    return true;
  }
  if (value == "false" || value == "0" || value == "off" || value == "no") {
    return false;
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<std::string> listComponentTypes() {
  return {"camera:perspective", "light:directional", "light:point",
          "light:spot",         "primitive:cone",    "primitive:cube",
          "primitive:cylinder", "primitive:plane",   "primitive:sphere"};
}

[[nodiscard]] std::vector<std::string>
completeComponentTypes(const CompletionContext &context) {
  std::vector<std::string> matches;
  for (const auto &type : listComponentTypes()) {
    if (type.rfind(context.partialToken, 0) == 0) {
      matches.push_back(type);
    }
  }
  return matches;
}

[[nodiscard]] std::vector<std::string>
completeCamActions(const CompletionContext &context) {
  static const std::vector<std::string> kActions = {
      "control", "fov", "look-at", "reset", "reset-editor-to-game"};
  std::vector<std::string> matches;
  for (const auto &action : kActions) {
    if (action.rfind(context.partialToken, 0) == 0) {
      matches.push_back(action);
    }
  }
  return matches;
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>>
splitFieldPath(const std::string &text) {
  constexpr std::string_view kNodeMaterialMarker = ".nodeMaterial.";
  if (const usize marker = text.find(kNodeMaterialMarker);
      marker != std::string::npos && marker > 0) {
    return std::make_pair(text.substr(0, marker), text.substr(marker + 1));
  }

  constexpr std::string_view kNodeMaterialBaseColorSuffix =
      ".nodeMaterial.baseColor";
  if (text.size() > kNodeMaterialBaseColorSuffix.size() &&
      text.compare(text.size() - kNodeMaterialBaseColorSuffix.size(),
                   kNodeMaterialBaseColorSuffix.size(),
                   kNodeMaterialBaseColorSuffix) == 0) {
    return std::make_pair(
        text.substr(0, text.size() - kNodeMaterialBaseColorSuffix.size()),
        "nodeMaterial.baseColor");
  }

  const usize dot = text.find_last_of('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= text.size()) {
    return std::nullopt;
  }
  return std::make_pair(text.substr(0, dot), text.substr(dot + 1));
}

[[nodiscard]] std::optional<std::reference_wrapper<CameraComponent>>
findActiveCamera(Scene &scene, EditorState &editorState) {
  if (const auto selected = editorState.getPrimarySelected()) {
    auto selectedCamera = selected->get().getComponent<CameraComponent>();
    if (selectedCamera.has_value()) {
      return std::ref(selectedCamera->get());
    }
  }

  if (const auto activeNode = editorState.resolveActiveCamera(scene)) {
    auto activeCamera = activeNode->getComponent<CameraComponent>();
    if (activeCamera.has_value()) {
      return std::ref(activeCamera->get());
    }
  }

  for (const auto &cameraNode : scene.getCameras()) {
    if (!cameraNode) {
      continue;
    }
    auto camera = cameraNode->getComponent<CameraComponent>();
    if (camera.has_value()) {
      return std::ref(camera->get());
    }
  }
  return std::nullopt;
}

[[nodiscard]] CommandResult getField(SceneNode &node,
                                     const std::string &field) {
  if (field == "translation") {
    const Vec3f value = node.getTranslation();
    return makeOk("translation = (" + formatFloat(value.x) + ", " +
                      formatFloat(value.y) + ", " + formatFloat(value.z) + ")",
                  "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "scale") {
    const Vec3f value = node.getScale();
    return makeOk("scale = (" + formatFloat(value.x) + ", " +
                      formatFloat(value.y) + ", " + formatFloat(value.z) + ")",
                  "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "rotation") {
    const Quatf value = node.getRotation();
    return makeOk("rotation = (" + formatFloat(value.w) + ", " +
                      formatFloat(value.v.x) + ", " + formatFloat(value.v.y) +
                      ", " + formatFloat(value.v.z) + ")",
                  "{\"value\":" + makeQuatJson(value) + "}");
  }
  if (field == "fov") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: fov");
    }
    const float value = camera->get().getFovY();
    return makeOk("fov = " + formatFloat(value),
                  "{\"value\":" + formatFloat(value) + "}");
  }
  if (field == "visibilityMask") {
    const u32 value = node.getVisibilityLayerMask();
    return makeOk("visibilityMask = " + std::to_string(value),
                  makeUnsignedJson(value));
  }
  if (field == "near") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: near");
    }
    return makeOk("near = " + formatFloat(camera->get().getNearPlane()),
                  "{\"value\":" + formatFloat(camera->get().getNearPlane()) +
                      "}");
  }
  if (field == "far") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: far");
    }
    return makeOk("far = " + formatFloat(camera->get().getFarPlane()),
                  "{\"value\":" + formatFloat(camera->get().getFarPlane()) +
                      "}");
  }
  if (field == "projection") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: projection");
    }
    const std::string value =
        camera->get().getProjectionType() == CameraType::Perspective
            ? "perspective"
            : "orthographic";
    return makeOk("projection = " + value,
                  "{\"value\":\"" + jsonEscape(value) + "\"}");
  }
  if (field == "cullingMask") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: cullingMask");
    }
    const u32 value = camera->get().getCullingMask();
    return makeOk("cullingMask = " + std::to_string(value),
                  makeUnsignedJson(value));
  }
  if (field == "light.kind" || field == "kind") {
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: light.kind");
    }
    const std::string value = lightKindName(light);
    return makeOk("light.kind = " + value,
                  "{\"value\":\"" + jsonEscape(value) + "\"}");
  }
  if (field == "light.direction" || field == "direction") {
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: direction");
    }
    Vec3f value{};
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      value = directional->getDirection();
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      value = spot->getDirection();
    } else {
      return makeError("field not available on node: direction");
    }
    return makeOk("direction = (" + formatFloat(value.x) + ", " +
                      formatFloat(value.y) + ", " + formatFloat(value.z) + ")",
                  "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "light.color" || field == "color") {
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: color");
    }
    Vec3f value{};
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      value = directional->getColor();
    } else if (const auto point =
                   std::dynamic_pointer_cast<PointLight>(light)) {
      value = point->getColor();
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      value = spot->getColor();
    }
    return makeOk("color = (" + formatFloat(value.x) + ", " +
                      formatFloat(value.y) + ", " + formatFloat(value.z) + ")",
                  "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "light.intensity" || field == "intensity") {
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: intensity");
    }
    float value = 0.0f;
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      value = directional->getIntensity();
    } else if (const auto point =
                   std::dynamic_pointer_cast<PointLight>(light)) {
      value = point->getIntensity();
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      value = spot->getIntensity();
    }
    return makeOk("intensity = " + formatFloat(value),
                  "{\"value\":" + formatFloat(value) + "}");
  }
  if (field == "light.shadowStrength" || field == "shadowStrength") {
    const auto directional = resolveDirectionalLight(node);
    if (!directional) {
      return makeError("field not available on node: shadowStrength");
    }
    const float value = directional->getShadowParams().z;
    return makeOk("shadowStrength = " + formatFloat(value),
                  "{\"value\":" + formatFloat(value) + "}");
  }
  if (field == "light.shadowDistance" || field == "shadowDistance") {
    const auto directional = resolveDirectionalLight(node);
    if (!directional) {
      return makeError("field not available on node: shadowDistance");
    }
    const float value = directional->getShadowDistance();
    return makeOk("shadowDistance = " + formatFloat(value),
                  "{\"value\":" + formatFloat(value) + "}");
  }
  if (field == "light.shadowCascadeCount" || field == "shadowCascadeCount") {
    const auto directional = resolveDirectionalLight(node);
    if (!directional) {
      return makeError("field not available on node: shadowCascadeCount");
    }
    const u32 value = directional->getShadowCascadeCount();
    return makeOk("shadowCascadeCount = " + std::to_string(value),
                  makeUnsignedJson(value));
  }
  if (field == "light.castsShadow" || field == "castsShadow") {
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: castsShadow");
    }
    const bool value = light->supportsPass(Pass_Shadow);
    return makeOk(std::string("castsShadow = ") + (value ? "true" : "false"),
                  std::string("{\"value\":") + (value ? "true" : "false") +
                      "}");
  }
  if (field == "light.range" || field == "range") {
    const auto light = resolveLight(node);
    if (const auto point = std::dynamic_pointer_cast<PointLight>(light)) {
      return makeOk("range = " + formatFloat(point->getRange()),
                    "{\"value\":" + formatFloat(point->getRange()) + "}");
    }
    if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      return makeOk("range = " + formatFloat(spot->getRange()),
                    "{\"value\":" + formatFloat(spot->getRange()) + "}");
    }
    return makeError("field not available on node: range");
  }
  if (field == "light.innerConeDegrees" || field == "innerConeDegrees") {
    const auto light = std::dynamic_pointer_cast<SpotLight>(resolveLight(node));
    if (!light) {
      return makeError("field not available on node: innerConeDegrees");
    }
    return makeOk(
        "innerConeDegrees = " + formatFloat(light->getInnerConeDegrees()),
        "{\"value\":" + formatFloat(light->getInnerConeDegrees()) + "}");
  }
  if (field == "light.outerConeDegrees" || field == "outerConeDegrees") {
    const auto light = std::dynamic_pointer_cast<SpotLight>(resolveLight(node));
    if (!light) {
      return makeError("field not available on node: outerConeDegrees");
    }
    return makeOk(
        "outerConeDegrees = " + formatFloat(light->getOuterConeDegrees()),
        "{\"value\":" + formatFloat(light->getOuterConeDegrees()) + "}");
  }
  if (field == "name") {
    const std::string value = node.getName();
    return makeOk("name = " + value,
                  "{\"value\":\"" + jsonEscape(value) + "\"}");
  }

  return makeError("unknown field: " + field);
}

struct NodeMaterialTarget final {
  std::string binding;
  std::string member;
  std::string key;
};

[[nodiscard]] std::optional<NodeMaterialTarget>
parseNodeMaterialTarget(const std::string &field) {
  constexpr std::string_view prefix = "nodeMaterial.";
  if (field.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const std::string rest = field.substr(prefix.size());
  const usize dot = rest.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= rest.size()) {
    return std::nullopt;
  }
  NodeMaterialTarget target;
  target.binding = rest.substr(0, dot);
  target.member = rest.substr(dot + 1);
  target.key = target.binding + "." + target.member;
  return target;
}

[[nodiscard]] std::optional<MaterialParameterValue>
parseMaterialParameterValue(const ShaderPropertyType type,
                            const std::vector<std::string> &args,
                            const usize valueStartIndex) {
  MaterialParameterValue value;
  switch (type) {
  case ShaderPropertyType::Float: {
    if (args.size() != valueStartIndex + 1) {
      return std::nullopt;
    }
    const auto parsed = parseFloat(args[valueStartIndex]);
    if (!parsed) {
      return std::nullopt;
    }
    value.type = MaterialParameterValueType::Float;
    value.floatValue = *parsed;
    return value;
  }
  case ShaderPropertyType::Int: {
    if (args.size() != valueStartIndex + 1) {
      return std::nullopt;
    }
    const auto parsed = parseInt(args[valueStartIndex]);
    if (!parsed) {
      return std::nullopt;
    }
    value.type = MaterialParameterValueType::Int;
    value.intValue = *parsed;
    return value;
  }
  case ShaderPropertyType::Vec3: {
    if (args.size() != valueStartIndex + 3) {
      return std::nullopt;
    }
    const auto parsed = parseVec3(args, valueStartIndex);
    if (!parsed) {
      return std::nullopt;
    }
    value.type = MaterialParameterValueType::Vec3;
    value.vectorValue = Vec4f{parsed->x, parsed->y, parsed->z, 0.0f};
    return value;
  }
  case ShaderPropertyType::Vec4: {
    if (args.size() != valueStartIndex + 4) {
      return std::nullopt;
    }
    const auto parsed = parseVec4(args, valueStartIndex);
    if (!parsed) {
      return std::nullopt;
    }
    value.type = MaterialParameterValueType::Vec4;
    value.vectorValue = *parsed;
    return value;
  }
  default:
    return std::nullopt;
  }
}

[[nodiscard]] CommandResult getMaterialField(const SceneIoContext &context,
                                             const std::string &path,
                                             const std::string &field) {
  if (field == "materialUri") {
    if (!context.getMaterialUri) {
      return makeError("material editing unavailable: material URI callback is "
                       "not registered");
    }
    const auto value = context.getMaterialUri(path);
    if (!value.has_value()) {
      return makeError("material URI not available on node: " + path);
    }
    return makeOk("materialUri = " + *value,
                  "{\"value\":\"" + jsonEscape(*value) + "\"}");
  }
  if (field == "nodeMaterial.baseColor") {
    if (!context.getNodeMaterialBaseColor) {
      return makeError(
          "material editing unavailable: baseColor callback is not registered");
    }
    const auto value = context.getNodeMaterialBaseColor(path);
    if (!value.has_value()) {
      return makeError("node material baseColor not available on node: " +
                       path);
    }
    return makeOk("nodeMaterial.baseColor = (" + formatFloat(value->x) + ", " +
                      formatFloat(value->y) + ", " + formatFloat(value->z) +
                      ")",
                  "{\"value\":" + makeVec3Json(*value) + "}");
  }
  if (const auto target = parseNodeMaterialTarget(field); target.has_value()) {
    if (!context.getNodeMaterialParameter) {
      return makeError(
          "material editing unavailable: parameter callback is not registered");
    }
    const auto value =
        context.getNodeMaterialParameter(path, target->binding, target->member);
    if (!value.has_value()) {
      return makeError("node material parameter not available on node: " +
                       target->key);
    }
    return makeOk(
        "nodeMaterial." + target->key + " = " + makeMaterialValueJson(*value),
        "{\"binding\":\"" + jsonEscape(target->binding) + "\",\"member\":\"" +
            jsonEscape(target->member) + "\",\"type\":\"" +
            materialParameterTypeName(value->type) +
            "\",\"value\":" + makeMaterialValueJson(*value) + "}");
  }
  return makeError("unknown field: " + field);
}

[[nodiscard]] std::string buildSetInverseCommand(SceneNode &node,
                                                 const std::string &field) {
  const std::string path = node.getPath();
  if (field == "translation") {
    const Vec3f value = node.getTranslation();
    return "set " + quoteToken(path + ".translation") + " " +
           formatFloat(value.x) + " " + formatFloat(value.y) + " " +
           formatFloat(value.z);
  }
  if (field == "scale") {
    const Vec3f value = node.getScale();
    return "set " + quoteToken(path + ".scale") + " " + formatFloat(value.x) +
           " " + formatFloat(value.y) + " " + formatFloat(value.z);
  }
  if (field == "rotation") {
    const Vec3f value = quatToEulerDegrees(node.getRotation());
    return "set " + quoteToken(path + ".rotation") + " " +
           formatFloat(value.x) + " " + formatFloat(value.y) + " " +
           formatFloat(value.z);
  }
  if (field == "visibilityMask") {
    return "set " + quoteToken(path + ".visibilityMask") + " " +
           std::to_string(node.getVisibilityLayerMask());
  }
  if (field == "name") {
    return "set " + quoteToken(path + ".name") + " " +
           quoteToken(node.getName());
  }

  const auto camera = node.getComponent<CameraComponent>();
  if (field == "fov" && camera.has_value()) {
    return "set " + quoteToken(path + ".fov") + " " +
           formatFloat(camera->get().getFovY());
  }
  if (field == "near" && camera.has_value()) {
    return "set " + quoteToken(path + ".near") + " " +
           formatFloat(camera->get().getNearPlane());
  }
  if (field == "far" && camera.has_value()) {
    return "set " + quoteToken(path + ".far") + " " +
           formatFloat(camera->get().getFarPlane());
  }
  if (field == "projection" && camera.has_value()) {
    const std::string projection =
        camera->get().getProjectionType() == CameraType::Perspective
            ? "perspective"
            : "orthographic";
    return "set " + quoteToken(path + ".projection") + " " + projection;
  }
  if (field == "cullingMask" && camera.has_value()) {
    return "set " + quoteToken(path + ".cullingMask") + " " +
           std::to_string(camera->get().getCullingMask());
  }

  const auto light = resolveLight(node);
  if ((field == "light.direction" || field == "direction") && light) {
    Vec3f value{};
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      value = directional->getDirection();
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      value = spot->getDirection();
    } else {
      return {};
    }
    return "set " + quoteToken(path + ".light.direction") + " " +
           formatFloat(value.x) + " " + formatFloat(value.y) + " " +
           formatFloat(value.z);
  }
  if ((field == "light.color" || field == "color") && light) {
    Vec3f value{};
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      value = directional->getColor();
    } else if (const auto point =
                   std::dynamic_pointer_cast<PointLight>(light)) {
      value = point->getColor();
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      value = spot->getColor();
    }
    return "set " + quoteToken(path + ".light.color") + " " +
           formatFloat(value.x) + " " + formatFloat(value.y) + " " +
           formatFloat(value.z);
  }
  if ((field == "light.intensity" || field == "intensity") && light) {
    float value = 0.0f;
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      value = directional->getIntensity();
    } else if (const auto point =
                   std::dynamic_pointer_cast<PointLight>(light)) {
      value = point->getIntensity();
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      value = spot->getIntensity();
    }
    return "set " + quoteToken(path + ".light.intensity") + " " +
           formatFloat(value);
  }
  if ((field == "light.castsShadow" || field == "castsShadow") && light) {
    return "set " + quoteToken(path + ".light.castsShadow") + " " +
           (light->supportsPass(Pass_Shadow) ? "true" : "false");
  }
  if ((field == "light.range" || field == "range") && light) {
    if (const auto point = std::dynamic_pointer_cast<PointLight>(light)) {
      return "set " + quoteToken(path + ".light.range") + " " +
             formatFloat(point->getRange());
    }
    if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      return "set " + quoteToken(path + ".light.range") + " " +
             formatFloat(spot->getRange());
    }
  }
  if ((field == "light.innerConeDegrees" || field == "innerConeDegrees") &&
      light) {
    if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      return "set " + quoteToken(path + ".light.innerConeDegrees") + " " +
             formatFloat(spot->getInnerConeDegrees());
    }
  }
  if ((field == "light.outerConeDegrees" || field == "outerConeDegrees") &&
      light) {
    if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      return "set " + quoteToken(path + ".light.outerConeDegrees") + " " +
             formatFloat(spot->getOuterConeDegrees());
    }
  }

  return {};
}

[[nodiscard]] std::string
buildMaterialSetInverseCommand(const SceneIoContext &context,
                               const std::string &path,
                               const std::string &field) {
  if (field == "materialUri" && context.getMaterialUri) {
    if (const auto value = context.getMaterialUri(path); value.has_value()) {
      return "set " + quoteToken(path + ".materialUri") + " " +
             quoteToken(*value);
    }
  }
  if (field == "nodeMaterial.baseColor" && context.getNodeMaterialBaseColor) {
    if (const auto value = context.getNodeMaterialBaseColor(path);
        value.has_value()) {
      return "set " + quoteToken(path + ".nodeMaterial.baseColor") + " " +
             formatFloat(value->x) + " " + formatFloat(value->y) + " " +
             formatFloat(value->z);
    }
  }
  if (const auto target = parseNodeMaterialTarget(field);
      target.has_value() && context.getNodeMaterialParameter) {
    const auto value =
        context.getNodeMaterialParameter(path, target->binding, target->member);
    if (!value.has_value()) {
      return {};
    }
    std::ostringstream oss;
    oss << "set " << quoteToken(path + ".nodeMaterial." + target->key) << ' ';
    switch (value->type) {
    case MaterialParameterValueType::Float:
      oss << formatFloat(value->floatValue);
      break;
    case MaterialParameterValueType::Int:
      oss << value->intValue;
      break;
    case MaterialParameterValueType::Vec3:
      oss << formatFloat(value->vectorValue.x) << ' '
          << formatFloat(value->vectorValue.y) << ' '
          << formatFloat(value->vectorValue.z);
      break;
    case MaterialParameterValueType::Vec4:
      oss << formatFloat(value->vectorValue.x) << ' '
          << formatFloat(value->vectorValue.y) << ' '
          << formatFloat(value->vectorValue.z) << ' '
          << formatFloat(value->vectorValue.w);
      break;
    }
    return oss.str();
  }
  return {};
}

[[nodiscard]] std::vector<std::string>
completeScenePaths(const Scene &scene, const CompletionContext &context) {
  std::vector<std::string> matches;
  for (const auto &path : scene.listAllPaths()) {
    if (path.rfind(context.partialToken, 0) == 0) {
      matches.push_back(path);
    }
  }
  return matches;
}

[[nodiscard]] std::vector<std::string> listEditableFields(SceneNode &node) {
  std::vector<std::string> fields = {"name", "rotation", "scale", "translation",
                                     "visibilityMask"};
  if (node.getComponent<CameraComponent>().has_value()) {
    fields.push_back("cullingMask");
    fields.push_back("far");
    fields.push_back("fov");
    fields.push_back("near");
    fields.push_back("projection");
  }
  if (const auto light = resolveLight(node)) {
    fields.push_back("light.castsShadow");
    fields.push_back("light.color");
    fields.push_back("light.intensity");
    fields.push_back("light.kind");
    if (std::dynamic_pointer_cast<DirectionalLight>(light) ||
        std::dynamic_pointer_cast<SpotLight>(light)) {
      fields.push_back("light.direction");
    }
    if (std::dynamic_pointer_cast<DirectionalLight>(light)) {
      fields.push_back("light.shadowCascadeCount");
      fields.push_back("light.shadowDistance");
      fields.push_back("light.shadowStrength");
    }
    if (std::dynamic_pointer_cast<PointLight>(light) ||
        std::dynamic_pointer_cast<SpotLight>(light)) {
      fields.push_back("light.range");
    }
    if (std::dynamic_pointer_cast<SpotLight>(light)) {
      fields.push_back("light.innerConeDegrees");
      fields.push_back("light.outerConeDegrees");
    }
  }
  fields.push_back("materialUri");
  fields.push_back("nodeMaterial.baseColor");
  std::sort(fields.begin(), fields.end());
  return fields;
}

[[nodiscard]] std::vector<std::string>
completeSetTarget(const Scene &scene, const CompletionContext &context) {
  const usize dot = context.partialToken.find_last_of('.');
  if (dot == std::string::npos) {
    return completeScenePaths(scene, context);
  }

  const std::string pathPrefix = context.partialToken.substr(0, dot);
  const std::string fieldPrefix = context.partialToken.substr(dot + 1);
  if (pathPrefix.empty()) {
    return {};
  }

  SceneNode *node = scene.findByPath(pathPrefix);
  if (!node) {
    return {};
  }

  std::vector<std::string> matches;
  for (const auto &field : listEditableFields(*node)) {
    if (field.rfind(fieldPrefix, 0) == 0) {
      matches.push_back(pathPrefix + "." + field);
    }
  }
  return matches;
}

struct CommandNodeStashEntry {
  SceneNodeSharedPtr node;
  SceneNodeSharedPtr parent;
  std::vector<std::pair<SceneNodeSharedPtr, LightBaseSharedPtr>> attachedLights;
};

struct BuiltinCommandState {
  u64 nextStashId = 1;
  u64 nextNodeSerial = 1;
  std::unordered_map<std::string, CommandNodeStashEntry> stash;
  std::optional<NodeClipboardEntry> clipboard;
};

[[nodiscard]] std::string allocateStashId(BuiltinCommandState &state) {
  return std::to_string(state.nextStashId++);
}

[[nodiscard]] bool sceneContainsNodeName(const Scene &scene,
                                         const std::string &nodeName) {
  for (const auto &renderable : scene.getRenderables()) {
    if (renderable && renderable->getNodeName() == nodeName) {
      return true;
    }
  }
  for (const auto &cameraNode : scene.getCameras()) {
    if (cameraNode && cameraNode->getNodeName() == nodeName) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string sanitizeNodeNameToken(std::string text) {
  if (text.empty()) {
    return "node";
  }
  for (char &c : text) {
    const bool valid = std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    if (!valid) {
      c = '_';
    }
  }
  return text;
}

[[nodiscard]] std::string sanitizeSceneNodeDisplayName(std::string text) {
  if (text.empty()) {
    return "node";
  }
  for (char &c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool valid =
        std::isalnum(uc) != 0 || c == '_' || c == '-' || c == '.';
    if (!valid) {
      c = '_';
    }
  }
  return text;
}

[[nodiscard]] std::string makeUniqueNodeName(Scene &scene,
                                             BuiltinCommandState &state,
                                             const std::string &base) {
  std::string candidate = sanitizeNodeNameToken(base);
  while (sceneContainsNodeName(scene, candidate)) {
    candidate = sanitizeNodeNameToken(base) + "_" +
                std::to_string(state.nextNodeSerial++);
  }
  return candidate;
}

[[nodiscard]] std::optional<Vec3f>
parseOptionalAddPlacement(const std::vector<std::string> &args,
                          usize &parentPathIndex, CommandResult &error) {
  parentPathIndex = args.size();
  if (args.size() == 2) {
    return std::nullopt;
  }
  if (args.size() == 3) {
    parentPathIndex = 2;
    return std::nullopt;
  }
  if (args.size() == 5) {
    const auto placement = parseVec3(args, 2);
    if (!placement) {
      error = makeError("invalid float for add placement");
      return std::nullopt;
    }
    return placement;
  }
  if (args.size() == 6) {
    parentPathIndex = 2;
    const auto placement = parseVec3(args, 3);
    if (!placement) {
      error = makeError("invalid float for add placement");
      return std::nullopt;
    }
    return placement;
  }
  error = makeError("usage: add <kind> <name> [parentPath] [x y z]");
  return std::nullopt;
}

[[nodiscard]] Vec3f defaultPlacementFromEditorCamera(Scene &scene,
                                                     EditorState &editorState) {
  const SceneNodeSharedPtr cameraNode = editorState.resolveActiveCamera(scene);
  if (!cameraNode) {
    return Vec3f{0.0f, 0.0f, 0.0f};
  }
  const auto camera = cameraNode->getComponent<CameraComponent>();
  if (!camera.has_value()) {
    return cameraNode->getTranslation();
  }
  const Vec3f eye = camera->get().getEyePosition();
  Vec3f forward = camera->get().getLookTarget() - eye;
  if (forward.length2() <= 1e-6f) {
    forward = Vec3f{0.0f, 0.0f, -1.0f};
  } else {
    forward = forward.normalized();
  }
  return eye + forward * 5.0f;
}

void copyActiveCameraPose(Scene &scene, EditorState &editorState,
                          SceneNode &node, CameraComponent &camera) {
  const SceneNodeSharedPtr sourceNode = editorState.resolveActiveCamera(scene);
  if (!sourceNode) {
    camera.updateMatrices();
    return;
  }
  const auto sourceCamera = sourceNode->getComponent<CameraComponent>();
  if (!sourceCamera.has_value()) {
    camera.updateMatrices();
    return;
  }
  const CameraComponent &source = sourceCamera->get();
  camera.applyProjectionState(
      source.getProjectionType(), source.getFovY(), source.getAspect(),
      source.getNearPlane(), source.getFarPlane(), source.getLeft(),
      source.getRight(), source.getBottom(), source.getTop());
  camera.setTarget(source.getTarget().value_or(RenderTarget{}));
  camera.setCullingMask(source.getCullingMask());
  camera.lookAt(source.getEyePosition(), source.getLookTarget(),
                source.getUpVector());
  node.setLocalTransform(sourceNode->getLocalTransform());
  node.setTranslation(source.getEyePosition());
}

[[nodiscard]] bool isPrimitiveAddKind(const std::string &kind) {
  return kind == "primitive:cube" || kind == "primitive:sphere" ||
         kind == "primitive:plane" || kind == "primitive:cylinder" ||
         kind == "primitive:cone";
}

[[nodiscard]] bool isModelAddKind(const std::string &kind) {
  return kind.rfind("model:", 0) == 0 &&
         kind.size() > std::string("model:").size();
}

[[nodiscard]] std::string primitiveNameFromKind(const std::string &kind) {
  const usize colon = kind.find(':');
  return colon == std::string::npos ? kind : kind.substr(colon + 1);
}

[[nodiscard]] std::string modelAssetIdFromKind(const std::string &kind) {
  return kind.substr(std::string("model:").size());
}

void collectSubtreeNodes(const SceneNodeSharedPtr &node,
                         std::vector<SceneNodeSharedPtr> &out) {
  if (!node) {
    return;
  }
  out.push_back(node);
  for (const auto &child : node->getChildren()) {
    collectSubtreeNodes(child, out);
  }
}

[[nodiscard]] CameraClipboardState
captureCameraClipboardState(const CameraComponent &camera) {
  return CameraClipboardState{
      .type = camera.getProjectionType(),
      .fovY = camera.getFovY(),
      .aspect = camera.getAspect(),
      .nearPlane = camera.getNearPlane(),
      .farPlane = camera.getFarPlane(),
      .left = camera.getLeft(),
      .right = camera.getRight(),
      .bottom = camera.getBottom(),
      .top = camera.getTop(),
      .cullingMask = camera.getCullingMask(),
      .active = camera.isActive(),
      .target = camera.getTarget(),
  };
}

[[nodiscard]] DirectionalLightClipboardState
captureDirectionalLightClipboardState(const DirectionalLight &light) {
  return DirectionalLightClipboardState{
      .direction = light.getDirection(),
      .color = light.getColor(),
      .intensity = light.getIntensity(),
      .shadowStrength = light.getShadowParams().z,
      .shadowDistance = light.getShadowDistance(),
      .shadowCascadeCount = light.getShadowCascadeCount(),
  };
}

[[nodiscard]] NodeClipboardEntry
captureNodeClipboardEntry(Scene &scene, const SceneNode &node) {
  NodeClipboardEntry entry;
  entry.nodeName = node.getNodeName();
  entry.name = node.getName();
  entry.transform = node.getLocalTransform();
  entry.visibilityMask = node.getVisibilityLayerMask();

  if (const auto mesh = node.getComponent<MeshComponent>(); mesh.has_value()) {
    entry.mesh = mesh->get().getMesh();
  }
  if (const auto material = node.getComponent<MaterialComponent>();
      material.has_value()) {
    entry.material = material->get().getMaterialInstance();
  }
  if (const auto skeleton = node.getComponent<SkeletonComponent>();
      skeleton.has_value()) {
    entry.skeleton = skeleton->get().getSkeleton();
  }
  if (const auto camera = node.getComponent<CameraComponent>();
      camera.has_value()) {
    entry.camera = captureCameraClipboardState(camera->get());
  }
  if (const auto light = scene.getDirectionalLight(node)) {
    entry.directionalLight = captureDirectionalLightClipboardState(*light);
  }

  for (const auto &child : node.getChildren()) {
    if (child) {
      entry.children.push_back(captureNodeClipboardEntry(scene, *child));
    }
  }
  return entry;
}

[[nodiscard]] std::string uniqueNodeName(Scene &scene,
                                         const std::string &sourceNodeName) {
  const std::string base =
      stripCopySuffix(sourceNodeName.empty() ? "node" : sourceNodeName);
  auto exists = [&scene](const std::string &candidate) {
    for (const auto &renderable : scene.getRenderables()) {
      if (renderable && renderable->getNodeName() == candidate) {
        return true;
      }
    }
    return false;
  };

  std::string candidate = base + ".copy";
  if (!exists(candidate)) {
    return candidate;
  }
  for (u32 index = 1; index < 100000; ++index) {
    std::ostringstream suffix;
    suffix << ".copy." << std::setw(3) << std::setfill('0') << index;
    candidate = base + suffix.str();
    if (!exists(candidate)) {
      return candidate;
    }
  }
  return base + ".copy.overflow";
}

void applyCameraClipboardState(CameraComponent &camera,
                               const CameraClipboardState &state) {
  camera.applyProjectionState(state.type, state.fovY, state.aspect,
                              state.nearPlane, state.farPlane, state.left,
                              state.right, state.bottom, state.top);
  camera.setCullingMask(state.cullingMask);
  camera.setActive(state.active);
  camera.setTarget(state.target);
}

void applyDirectionalLightClipboardState(
    DirectionalLight &light, const DirectionalLightClipboardState &state) {
  light.setDirection(state.direction);
  light.setColor(state.color);
  light.setIntensity(state.intensity);
  light.setShadowStrength(state.shadowStrength);
  light.setShadowDistance(state.shadowDistance);
  light.setShadowCascadeCount(state.shadowCascadeCount);
}

[[nodiscard]] SceneNodeSharedPtr
instantiateClipboardSubtree(Scene &scene, const NodeClipboardEntry &entry,
                            const SceneNodeSharedPtr &parent,
                            const bool renameRootAsCopy,
                            const bool offsetRoot) {
  auto node = SceneNode::create(uniqueNodeName(scene, entry.nodeName));
  node->setName(renameRootAsCopy ? makeCopyName(parent, entry.name)
                                 : entry.name);
  Transform transform = entry.transform;
  if (offsetRoot) {
    transform.translation.x += kDuplicateOffsetX;
  }
  node->setLocalTransform(transform);
  node->setVisibilityLayerMask(entry.visibilityMask);
  if (entry.mesh) {
    (void)node->addComponent<MeshComponent>(entry.mesh);
  }
  if (entry.material) {
    (void)node->addComponent<MaterialComponent>(entry.material);
  }
  if (entry.skeleton) {
    (void)node->addComponent<SkeletonComponent>(entry.skeleton);
  }
  if (entry.camera.has_value()) {
    const auto camera = node->addComponent<CameraComponent>();
    if (camera.has_value()) {
      applyCameraClipboardState(camera->get(), *entry.camera);
    }
  }
  if (parent) {
    node->setParent(parent);
  }

  for (const auto &childEntry : entry.children) {
    (void)instantiateClipboardSubtree(scene, childEntry, node, false, false);
  }
  return node;
}

void attachClipboardDirectionalLights(Scene &scene,
                                      const NodeClipboardEntry &entry,
                                      const SceneNodeSharedPtr &node) {
  if (!node) {
    return;
  }
  if (entry.directionalLight.has_value()) {
    auto light = std::make_shared<DirectionalLight>();
    applyDirectionalLightClipboardState(*light, *entry.directionalLight);
    scene.attachLight(node, light);
  }

  const auto children = node->getChildren();
  const usize childCount = std::min(children.size(), entry.children.size());
  for (usize i = 0; i < childCount; ++i) {
    attachClipboardDirectionalLights(scene, entry.children[i], children[i]);
  }
}

void registerSubtreeWithScene(Scene &scene, const SceneNodeSharedPtr &node) {
  if (!node) {
    return;
  }

  if (node->getComponent<CameraComponent>().has_value()) {
    scene.addCamera(node);
  } else {
    scene.addRenderable(node);
  }
  for (const auto &child : node->getChildren()) {
    registerSubtreeWithScene(scene, child);
  }
}

[[nodiscard]] CommandResult removeNodeToStash(Scene &scene,
                                              EditorState &editorState,
                                              BuiltinCommandState &state,
                                              const std::string &path,
                                              const std::string &stashId) {
  SceneNode *node = nullptr;
  const CommandResult found = requireNode(scene, path, node);
  if (!found.ok) {
    return found;
  }
  if (node->isSceneRoot()) {
    return makeError("cannot remove scene root");
  }

  const auto removed = node->shared_from_this();
  CommandNodeStashEntry entry;
  entry.node = removed;
  entry.parent = removed->getParent();

  std::vector<SceneNodeSharedPtr> subtreeNodes;
  collectSubtreeNodes(removed, subtreeNodes);
  for (const auto &subtreeNode : subtreeNodes) {
    const auto light = scene.getLight(*subtreeNode);
    if (light) {
      entry.attachedLights.push_back({subtreeNode, light});
    }
  }

  scene.removeRenderable(removed);
  editorState.selectRemove(removed);
  state.stash[stashId] = std::move(entry);
  return makeOk("removed " + path, "{\"path\":\"" + jsonEscape(path) +
                                       "\",\"stash\":\"" + jsonEscape(stashId) +
                                       "\"}");
}

[[nodiscard]] CommandResult restoreNodeFromStash(Scene &scene,
                                                 BuiltinCommandState &state,
                                                 const std::string &stashId) {
  const auto stashIt = state.stash.find(stashId);
  if (stashIt == state.stash.end() || !stashIt->second.node) {
    return makeError("stash entry not found: " + stashId);
  }

  CommandNodeStashEntry &entry = stashIt->second;
  if (entry.parent) {
    entry.node->setParent(entry.parent);
  } else {
    entry.node->clearParent();
  }

  registerSubtreeWithScene(scene, entry.node);

  for (const auto &[attachedNode, light] : entry.attachedLights) {
    scene.attachLight(attachedNode, light);
  }

  return makeOk("restored " + entry.node->getPath(),
                "{\"path\":\"" + jsonEscape(entry.node->getPath()) +
                    "\",\"stash\":\"" + jsonEscape(stashId) + "\"}");
}

[[nodiscard]] CommandResult setField(SceneNode &node, const std::string &field,
                                     const std::vector<std::string> &args,
                                     const usize valueStartIndex) {
  if (field == "translation") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.translation <x> <y> <z>");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set translation");
    }
    node.setTranslation(*value);
    return makeOk("translation updated",
                  "{\"value\":" + makeVec3Json(*value) + "}");
  }
  if (field == "scale") {
    Vec3f value{};
    if (args.size() == valueStartIndex + 1) {
      const auto s = parseFloat(args[valueStartIndex]);
      if (!s) {
        return makeError("invalid float for set scale");
      }
      value = Vec3f{*s, *s, *s};
    } else if (args.size() == valueStartIndex + 3) {
      const auto parsed = parseVec3(args, valueStartIndex);
      if (!parsed) {
        return makeError("invalid float for set scale");
      }
      value = *parsed;
    } else {
      return makeError(
          "usage: set <path>.scale <s> | set <path>.scale <sx> <sy> <sz>");
    }
    node.setScale(value);
    return makeOk("scale updated", "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "rotation") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.rotation <rx-deg> <ry-deg> <rz-deg>");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set rotation");
    }
    const Quatf rotation = eulerDegreesToQuat(value->x, value->y, value->z);
    node.setRotation(rotation);
    return makeOk("rotation updated",
                  "{\"value\":" + makeQuatJson(rotation) + "}");
  }
  if (field == "fov") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.fov <degrees>");
    }
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: fov");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set fov");
    }
    camera->get().setFovY(*value);
    return makeOk("fov updated", "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "visibilityMask") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.visibilityMask <u32>");
    }
    const auto value = parseUnsigned(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid unsigned for set visibilityMask");
    }
    node.setVisibilityLayerMask(*value);
    return makeOk("visibilityMask updated", makeUnsignedJson(*value));
  }
  if (field == "near") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.near <value>");
    }
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: near");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set near");
    }
    camera->get().setNearPlane(*value);
    return makeOk("near updated", "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "far") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.far <value>");
    }
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: far");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set far");
    }
    camera->get().setFarPlane(*value);
    return makeOk("far updated", "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "projection") {
    if (args.size() != valueStartIndex + 1) {
      return makeError(
          "usage: set <path>.projection <perspective|orthographic>");
    }
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: projection");
    }
    const std::string value = lowerCopy(args[valueStartIndex]);
    if (value == "perspective") {
      camera->get().setProjectionType(CameraType::Perspective);
    } else if (value == "orthographic") {
      camera->get().setProjectionType(CameraType::Orthographic);
    } else {
      return makeError("invalid projection for set projection");
    }
    return makeOk("projection updated",
                  "{\"value\":\"" + jsonEscape(value) + "\"}");
  }
  if (field == "cullingMask") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.cullingMask <u32>");
    }
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: cullingMask");
    }
    const auto value = parseUnsigned(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid unsigned for set cullingMask");
    }
    camera->get().setCullingMask(*value);
    return makeOk("cullingMask updated", makeUnsignedJson(*value));
  }
  if (field == "light.direction" || field == "direction") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.light.direction <x> <y> <z>");
    }
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: direction");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set direction");
    }
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      directional->setDirection(*value);
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      spot->setDirection(*value);
    } else {
      return makeError("field not available on node: direction");
    }
    return makeOk("direction updated",
                  "{\"value\":" + makeVec3Json(*value) + "}");
  }
  if (field == "light.color" || field == "color") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.light.color <r> <g> <b>");
    }
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: color");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set color");
    }
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      directional->setColor(*value);
    } else if (const auto point =
                   std::dynamic_pointer_cast<PointLight>(light)) {
      point->setColor(*value);
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      spot->setColor(*value);
    }
    return makeOk("color updated", "{\"value\":" + makeVec3Json(*value) + "}");
  }
  if (field == "light.intensity" || field == "intensity") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.intensity <value>");
    }
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: intensity");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set intensity");
    }
    if (const auto directional =
            std::dynamic_pointer_cast<DirectionalLight>(light)) {
      directional->setIntensity(*value);
    } else if (const auto point =
                   std::dynamic_pointer_cast<PointLight>(light)) {
      point->setIntensity(*value);
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      spot->setIntensity(*value);
    }
    return makeOk("intensity updated",
                  "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "light.shadowStrength" || field == "shadowStrength") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.shadowStrength <value>");
    }
    const auto directional = resolveDirectionalLight(node);
    if (!directional) {
      return makeError("field not available on node: shadowStrength");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set shadowStrength");
    }
    directional->setShadowStrength(*value);
    return makeOk(
        "shadowStrength updated",
        "{\"value\":" + formatFloat(directional->getShadowParams().z) + "}");
  }
  if (field == "light.shadowDistance" || field == "shadowDistance") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.shadowDistance <value>");
    }
    const auto directional = resolveDirectionalLight(node);
    if (!directional) {
      return makeError("field not available on node: shadowDistance");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set shadowDistance");
    }
    directional->setShadowDistance(*value);
    return makeOk(
        "shadowDistance updated",
        "{\"value\":" + formatFloat(directional->getShadowDistance()) + "}");
  }
  if (field == "light.shadowCascadeCount" || field == "shadowCascadeCount") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.shadowCascadeCount <u32>");
    }
    const auto directional = resolveDirectionalLight(node);
    if (!directional) {
      return makeError("field not available on node: shadowCascadeCount");
    }
    const auto value = parseUnsigned(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid unsigned for set shadowCascadeCount");
    }
    directional->setShadowCascadeCount(*value);
    return makeOk("shadowCascadeCount updated",
                  makeUnsignedJson(directional->getShadowCascadeCount()));
  }
  if (field == "light.castsShadow" || field == "castsShadow") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.castsShadow <true|false>");
    }
    const auto light = resolveLight(node);
    if (!light) {
      return makeError("field not available on node: castsShadow");
    }
    const auto value = parseBoolToken(args[valueStartIndex]);
    if (!value.has_value()) {
      return makeError("invalid bool for set castsShadow");
    }
    setLightCastsShadow(light, *value);
    return makeOk(std::string("castsShadow updated"),
                  std::string("{\"value\":") + (*value ? "true" : "false") +
                      "}");
  }
  if (field == "light.range" || field == "range") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.range <value>");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set range");
    }
    const auto light = resolveLight(node);
    if (const auto point = std::dynamic_pointer_cast<PointLight>(light)) {
      point->setRange(*value);
    } else if (const auto spot = std::dynamic_pointer_cast<SpotLight>(light)) {
      spot->setRange(*value);
    } else {
      return makeError("field not available on node: range");
    }
    return makeOk("range updated", "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "light.innerConeDegrees" || field == "innerConeDegrees") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.innerConeDegrees <value>");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set innerConeDegrees");
    }
    const auto light = std::dynamic_pointer_cast<SpotLight>(resolveLight(node));
    if (!light) {
      return makeError("field not available on node: innerConeDegrees");
    }
    light->setInnerConeDegrees(*value);
    return makeOk("innerConeDegrees updated",
                  "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "light.outerConeDegrees" || field == "outerConeDegrees") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.light.outerConeDegrees <value>");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set outerConeDegrees");
    }
    const auto light = std::dynamic_pointer_cast<SpotLight>(resolveLight(node));
    if (!light) {
      return makeError("field not available on node: outerConeDegrees");
    }
    light->setOuterConeDegrees(*value);
    return makeOk("outerConeDegrees updated",
                  "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "name") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.name <value>");
    }
    if (siblingNameExists(node, args[valueStartIndex])) {
      return makeError("rename conflict: sibling already named " +
                       args[valueStartIndex]);
    }
    node.setName(args[valueStartIndex]);
    return makeOk("name updated",
                  "{\"value\":\"" + jsonEscape(node.getName()) + "\"}");
  }

  return makeError("unknown field: " + field);
}

[[nodiscard]] CommandResult
setMaterialField(const SceneIoContext &context, const std::string &path,
                 const std::string &field, const std::vector<std::string> &args,
                 const usize valueStartIndex) {
  if (field == "materialUri") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.materialUri <uri>");
    }
    if (!context.setMaterialUri) {
      return makeError("material editing unavailable: material URI callback is "
                       "not registered");
    }
    return context.setMaterialUri(path, args[valueStartIndex]);
  }
  if (field == "nodeMaterial.baseColor") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.nodeMaterial.baseColor <r> <g> <b>");
    }
    if (!context.setNodeMaterialBaseColor) {
      return makeError(
          "material editing unavailable: baseColor callback is not registered");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set nodeMaterial.baseColor");
    }
    return context.setNodeMaterialBaseColor(path, *value);
  }
  if (const auto target = parseNodeMaterialTarget(field); target.has_value()) {
    if (!context.getNodeMaterialParameter ||
        !context.setNodeMaterialParameter) {
      return makeError(
          "material editing unavailable: parameter callback is not registered");
    }
    const auto current =
        context.getNodeMaterialParameter(path, target->binding, target->member);
    if (!current.has_value()) {
      return makeError("material parameter not found: " + target->key);
    }
    ShaderPropertyType reflectedType = ShaderPropertyType::Float;
    switch (current->type) {
    case MaterialParameterValueType::Float:
      reflectedType = ShaderPropertyType::Float;
      break;
    case MaterialParameterValueType::Int:
      reflectedType = ShaderPropertyType::Int;
      break;
    case MaterialParameterValueType::Vec3:
      reflectedType = ShaderPropertyType::Vec3;
      break;
    case MaterialParameterValueType::Vec4:
      reflectedType = ShaderPropertyType::Vec4;
      break;
    }
    const auto value =
        parseMaterialParameterValue(reflectedType, args, valueStartIndex);
    if (!value.has_value()) {
      return makeError("invalid value for material parameter: " + target->key);
    }
    return context.setNodeMaterialParameter(path, target->binding,
                                            target->member, *value);
  }
  return makeError("unknown field: " + field);
}

[[nodiscard]] InverseFn inverseFromMetadata() {
  return [](const ParsedCommand &,
            const CommandResult &result) -> std::optional<std::string> {
    const auto it = result.metadata.find("inverse.line");
    if (it == result.metadata.end() || it->second.empty()) {
      return std::nullopt;
    }
    return it->second;
  };
}

} // namespace

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene,
                             const SceneIoContext &sceneIoContext) {
  const auto state = std::make_shared<BuiltinCommandState>();
  const auto sceneOpen = sceneIoContext.open;
  const auto sceneSave = sceneIoContext.save;
  const auto sceneList = sceneIoContext.list;
  const auto setAdmin = sceneIoContext.setAdmin;
  const auto adminStatus = sceneIoContext.adminStatus;
  const auto defaultAddPlacement = sceneIoContext.defaultAddPlacement;
  const auto createNode = sceneIoContext.createNode;
  const SceneIoContext materialContext = sceneIoContext;

  bus.registerHandler(
      "help", "help [verb]", [&bus](std::vector<std::string> args) {
        if (args.size() > 1) {
          return makeError("usage: help [verb]");
        }
        const std::string message = buildHelpMessage(bus, args);
        if (message.rfind("unknown command:", 0) == 0) {
          return makeError(message);
        }
        return makeOk(message, makeVerbListJson(bus.listVerbs()));
      });

  bus.registerHandler(
      "scene", "scene list | scene open <path> | scene save [path]",
      [sceneOpen, sceneSave, sceneList](std::vector<std::string> args) {
        if (args.empty()) {
          return makeError(
              "usage: scene list | scene open <path> | scene save [path]");
        }

        const std::string &action = args[0];
        if (action == "list") {
          if (args.size() != 1) {
            return makeError("usage: scene list");
          }
          if (!sceneList) {
            return makeSceneIoUnavailable("list");
          }
          return sceneList();
        }
        if (action == "open") {
          if (args.size() != 2) {
            return makeError("usage: scene open <path>");
          }
          if (!sceneOpen) {
            return makeSceneIoUnavailable("open");
          }
          return markClearsHistoryOnSuccess(sceneOpen(args[1]));
        }

        if (action == "save") {
          if (args.size() > 2) {
            return makeError("usage: scene save [path]");
          }
          if (!sceneSave) {
            return makeSceneIoUnavailable("save");
          }
          if (args.size() == 2) {
            return sceneSave(args[1]);
          }
          return sceneSave(std::nullopt);
        }

        return makeError("unknown scene action: " + action);
      });

  if (setAdmin || adminStatus) {
    bus.registerHandler("admin", "admin on | admin off | admin status",
                        [setAdmin, adminStatus](std::vector<std::string> args) {
                          if (args.size() != 1) {
                            return makeError(
                                "usage: admin on | admin off | admin status");
                          }
                          const std::string action = lowerCopy(args[0]);
                          if (action == "on") {
                            if (!setAdmin) {
                              return makeAdminUnavailable("on");
                            }
                            return setAdmin(true);
                          }
                          if (action == "off") {
                            if (!setAdmin) {
                              return makeAdminUnavailable("off");
                            }
                            return setAdmin(false);
                          }
                          if (action == "status") {
                            if (!adminStatus) {
                              return makeAdminUnavailable("status");
                            }
                            return adminStatus();
                          }
                          return makeError("unknown admin action: " + action);
                        });
  } else {
    bus.unregisterHandler("admin");
  }

  bus.registerHandler(
      "__remove_to_stash", "__remove_to_stash <path> <stash-id>",
      [&scene, &editorState, state](std::vector<std::string> args) {
        if (args.size() != 2) {
          return makeError("usage: __remove_to_stash <path> <stash-id>");
        }
        return removeNodeToStash(scene, editorState, *state, args[0], args[1]);
      });

  bus.registerHandler("__restore_from_stash", "__restore_from_stash <stash-id>",
                      [&scene, state](std::vector<std::string> args) {
                        if (args.size() != 1) {
                          return makeError(
                              "usage: __restore_from_stash <stash-id>");
                        }
                        return restoreNodeFromStash(scene, *state, args[0]);
                      });

  bus.registerHandler(
      "select",
      CommandMetadata{"select <path> [path...]", inverseFromMetadata(), true},
      [&editorState, &scene](std::vector<std::string> args) {
        CommandResult result;
        result.metadata["inverse.line"] =
            buildSelectionCommand(editorState.getSelected());

        if (args.empty()) {
          editorState.deselect();
          result.ok = true;
          result.message = "selection cleared";
          result.structured = "{\"selected\":null}";
          return result;
        }

        const auto nodes = resolveNodePaths(scene, args, result);
        if (!nodes.has_value()) {
          return result;
        }
        editorState.select(*nodes);

        std::ostringstream structured;
        structured << "{\"paths\":[";
        for (usize i = 0; i < nodes->size(); ++i) {
          if (i != 0) {
            structured << ',';
          }
          structured << '"' << jsonEscape((*nodes)[i]->getPath()) << '"';
        }
        structured << "]}";

        result.ok = true;
        result.message =
            "selected " + std::to_string(nodes->size()) + " node(s)";
        result.structured = structured.str();
        return result;
      });

  bus.registerHandler("deselect",
                      CommandMetadata{"deselect", inverseFromMetadata(), true},
                      [&editorState](std::vector<std::string> args) {
                        if (!args.empty()) {
                          return makeError("usage: deselect");
                        }
                        CommandResult result =
                            makeOk("selection cleared", "{\"selected\":null}");
                        result.metadata["inverse.line"] =
                            buildSelectionCommand(editorState.getSelected());
                        editorState.deselect();
                        return result;
                      });

  bus.registerHandler(
      "move",
      CommandMetadata{"move <path> <x> <y> <z> | move <path...> <dx> <dy> <dz>",
                      inverseFromMetadata(), true},
      [&scene](std::vector<std::string> args) {
        CommandResult error;
        const auto paths = extractTargetPathsAndVec3Args(
            args, error,
            "usage: move <path> <x> <y> <z> | move <path...> <dx> <dy> <dz>",
            "invalid float for move");
        if (!paths.has_value()) {
          return error;
        }
        const auto nodes = resolveNodePaths(scene, *paths, error);
        if (!nodes.has_value()) {
          return error;
        }

        const auto value = parseVec3(args, args.size() - 3);
        if (!value) {
          return makeError("invalid float for move");
        }

        std::vector<std::string> inverseLines;
        inverseLines.reserve(nodes->size());
        for (const auto &node : *nodes) {
          const Vec3f before = node->getTranslation();
          inverseLines.push_back("move " + quoteToken(node->getPath()) + " " +
                                 formatFloat(before.x) + " " +
                                 formatFloat(before.y) + " " +
                                 formatFloat(before.z));
        }

        if (nodes->size() == 1) {
          (*nodes)[0]->setTranslation(*value);
          const bool touchedLight =
              syncLightSpatialProperties(scene, (*nodes)[0]);
          CommandResult result =
              makeOk("moved " + (*nodes)[0]->getPath() + " to (" +
                         formatFloat(value->x) + ", " + formatFloat(value->y) +
                         ", " + formatFloat(value->z) + ")",
                     makeVec3Json(*value));
          result.metadata["inverse.line"] = inverseLines.front();
          if (touchedLight) {
            result.metadata["scene.rebuild"] = "true";
          }
          return result;
        }

        bool touchedLight = false;
        for (const auto &node : *nodes) {
          node->setTranslation(node->getTranslation() + *value);
          touchedLight = syncLightSpatialProperties(scene, node) || touchedLight;
        }

        CommandResult result = makeOk(
            "moved " + std::to_string(nodes->size()) + " node(s) by delta (" +
                formatFloat(value->x) + ", " + formatFloat(value->y) + ", " +
                formatFloat(value->z) + ")",
            makeVec3Json(*value));
        result.metadata["inverse.line"] = joinLines(inverseLines);
        if (touchedLight) {
          result.metadata["scene.rebuild"] = "true";
        }
        return result;
      });

  bus.registerHandler(
      "rotate",
      CommandMetadata{"rotate <path> <rx-deg> <ry-deg> <rz-deg> | rotate "
                      "<path...> <rx-deg> <ry-deg> <rz-deg>",
                      inverseFromMetadata(), true},
      [&scene](std::vector<std::string> args) {
        CommandResult error;
        const auto paths = extractTargetPathsAndVec3Args(
            args, error,
            "usage: rotate <path> <rx-deg> <ry-deg> <rz-deg> | rotate "
            "<path...> <rx-deg> <ry-deg> <rz-deg>",
            "invalid float for rotate");
        if (!paths.has_value()) {
          return error;
        }
        const auto nodes = resolveNodePaths(scene, *paths, error);
        if (!nodes.has_value()) {
          return error;
        }

        const auto value = parseVec3(args, args.size() - 3);
        if (!value) {
          return makeError("invalid float for rotate");
        }
        const Quatf rotation = eulerDegreesToQuat(value->x, value->y, value->z);

        std::vector<std::string> inverseLines;
        inverseLines.reserve(nodes->size());
        bool touchedLight = false;
        for (const auto &node : *nodes) {
          const Vec3f before = quatToEulerDegrees(node->getRotation());
          inverseLines.push_back("rotate " + quoteToken(node->getPath()) + " " +
                                 formatFloat(before.x) + " " +
                                 formatFloat(before.y) + " " +
                                 formatFloat(before.z));
          if (nodes->size() == 1) {
            node->setRotation(rotation);
          } else {
            node->setRotation((rotation * node->getRotation()).normalized());
          }
          touchedLight = syncLightSpatialProperties(scene, node) || touchedLight;
        }

        CommandResult result = makeOk(
            nodes->size() == 1 ? "rotated " + (*nodes)[0]->getPath()
                               : "rotated " + std::to_string(nodes->size()) +
                                     " node(s) by delta",
            makeQuatJson(rotation));
        result.metadata["inverse.line"] =
            nodes->size() == 1 ? inverseLines.front() : joinLines(inverseLines);
        if (touchedLight) {
          result.metadata["scene.rebuild"] = "true";
        }
        return result;
      });

  bus.registerHandler(
      "scale",
      CommandMetadata{"scale <path> <s> | scale <path> <sx> <sy> <sz>",
                      inverseFromMetadata(), true},
      [&scene](std::vector<std::string> args) {
        CommandResult error;
        const auto paths = extractTargetPathsAndScaleArgs(args, error);
        if (!paths.has_value()) {
          return error;
        }
        const auto nodes = resolveNodePaths(scene, *paths, error);
        if (!nodes.has_value()) {
          return error;
        }

        Vec3f scale{};
        const usize valueStartIndex = paths->size();
        if (args.size() == valueStartIndex + 1) {
          const auto s = parseFloat(args[valueStartIndex]);
          if (!s) {
            return makeError("invalid float for scale");
          }
          scale = Vec3f{*s, *s, *s};
        } else {
          const auto parsed = parseVec3(args, valueStartIndex);
          if (!parsed) {
            return makeError("invalid float for scale");
          }
          scale = *parsed;
        }

        std::vector<std::string> inverseLines;
        inverseLines.reserve(nodes->size());
        for (const auto &node : *nodes) {
          const Vec3f before = node->getScale();
          inverseLines.push_back("scale " + quoteToken(node->getPath()) + " " +
                                 formatFloat(before.x) + " " +
                                 formatFloat(before.y) + " " +
                                 formatFloat(before.z));
          if (nodes->size() == 1) {
            node->setScale(scale);
          } else {
            node->setScale(Vec3f{before.x * scale.x, before.y * scale.y,
                                 before.z * scale.z});
          }
        }

        CommandResult result = makeOk(
            nodes->size() == 1 ? "scaled " + (*nodes)[0]->getPath()
                               : "scaled " + std::to_string(nodes->size()) +
                                     " node(s) by ratio",
            makeVec3Json(scale));
        result.metadata["inverse.line"] =
            nodes->size() == 1 ? inverseLines.front() : joinLines(inverseLines);
        return result;
      });

  bus.registerHandler(
      "add",
      CommandMetadata{
          "add "
          "(primitive:cube|primitive:sphere|primitive:plane|"
          "primitive:cylinder|primitive:cone|light:directional|"
          "light:point|light:spot|camera:perspective|model:<id>) <name> "
          "[parentPath] [x y z]",
          inverseFromMetadata(), true},
      [&scene, &editorState, state, defaultAddPlacement,
       createNode](std::vector<std::string> args) {
        if (args.size() < 2) {
          return makeError("usage: add <kind> <name> [parentPath] [x y z]");
        }

        std::string kind = args[0];
        std::string lightKindName = "directional";
        if (kind == "camera") {
          kind = "camera:perspective";
        } else if (kind == "light") {
          kind = "light:directional";
        } else if (kind == "mesh") {
          return makeError(
              "add mesh is no longer supported; use add primitive:<shape>");
        }
        if (kind.rfind("light:", 0) == 0) {
          lightKindName = lowerCopy(kind.substr(std::string("light:").size()));
        }

        const std::string name = sanitizeSceneNodeDisplayName(args[1]);
        usize parentPathIndex = args.size();
        CommandResult placementError;
        const std::optional<Vec3f> explicitPlacement =
            parseOptionalAddPlacement(args, parentPathIndex, placementError);
        if (!placementError.message.empty()) {
          return placementError;
        }

        SceneNodeSharedPtr parent;
        if (parentPathIndex < args.size()) {
          SceneNode *parentNode = nullptr;
          const CommandResult found =
              requireNode(scene, args[parentPathIndex], parentNode);
          if (!found.ok) {
            return found;
          }
          parent = parentNode->shared_from_this();
        } else {
          parent = chooseCommandParent(editorState);
        }

        const bool primitiveKind = isPrimitiveAddKind(kind);
        const bool modelKind = isModelAddKind(kind);
        const bool cameraKind = kind == "camera:perspective";
        const bool lightKind = kind == "light:directional" ||
                               kind == "light:point" || kind == "light:spot";
        if (!primitiveKind && !modelKind && !cameraKind && !lightKind) {
          return makeError("unknown add target: " + kind);
        }

        const std::string nodeNameBase =
            primitiveKind
                ? "primitive_" + primitiveNameFromKind(kind) + "_node"
                : (modelKind ? "model_" + modelAssetIdFromKind(kind) + "_node"
                             : (cameraKind ? "camera_node"
                                           : lightKindName + "_light_node"));
        const std::string nodeName =
            makeUniqueNodeName(scene, *state, nodeNameBase);

        SceneNodeSharedPtr node;
        if (primitiveKind || modelKind) {
          if (!createNode) {
            return makeError("node creation is unavailable");
          }
          CommandResult created = createNode(kind, nodeName, name, node);
          if (!created.ok) {
            return created;
          }
          if (!node) {
            return makeError("node creation returned no node");
          }
        } else {
          node = SceneNode::create(nodeName);
          node->setName(name);
        }

        if (parent) {
          node->setParent(parent);
        }

        const Vec3f placement = explicitPlacement.value_or(
            defaultAddPlacement
                ? defaultAddPlacement()
                : defaultPlacementFromEditorCamera(scene, editorState));
        if (!cameraKind) {
          node->setTranslation(placement);
        }

        const std::string stashId = allocateStashId(*state);
        if (primitiveKind || modelKind) {
          scene.addRenderable(node);
        } else if (cameraKind) {
          const auto camera = node->addComponent<CameraComponent>();
          if (!camera.has_value()) {
            return makeError("failed to add camera component");
          }
          copyActiveCameraPose(scene, editorState, *node, camera->get());
          if (explicitPlacement.has_value()) {
            node->setTranslation(*explicitPlacement);
          }
          scene.addCamera(node);
        } else {
          scene.addRenderable(node);
          LightBaseSharedPtr light;
          if (lightKindName == "directional") {
            auto directional = std::make_shared<DirectionalLight>();
            directional->setDirection(Vec3f{-0.3f, -1.0f, -0.5f});
            directional->setColor(Vec3f{1.0f, 0.98f, 0.9f});
            directional->setIntensity(1.0f);
            light = directional;
          } else if (lightKindName == "point") {
            auto point = std::make_shared<PointLight>();
            point->setColor(Vec3f{1.0f, 0.98f, 0.9f});
            point->setIntensity(1.0f);
            point->setRange(5.0f);
            light = point;
          } else if (lightKindName == "spot") {
            auto spot = std::make_shared<SpotLight>();
            spot->setDirection(Vec3f{0.0f, -1.0f, 0.0f});
            spot->setColor(Vec3f{1.0f, 0.98f, 0.9f});
            spot->setIntensity(1.0f);
            spot->setRange(8.0f);
            spot->setInnerConeDegrees(20.0f);
            spot->setOuterConeDegrees(35.0f);
            light = spot;
          } else {
            return makeError("unknown light kind: " + lightKindName);
          }
          scene.attachLight(node, light);
        }

        CommandResult result =
            makeOk("added " + kind + " " + node->getPath(),
                   "{\"path\":\"" + jsonEscape(node->getPath()) +
                       "\",\"kind\":\"" + jsonEscape(kind) + "\"}");
        result.metadata["inverse.line"] = "__remove_to_stash " +
                                          quoteToken(node->getPath()) + " " +
                                          quoteToken(stashId);
        result.metadata["redo.line"] =
            "__restore_from_stash " + quoteToken(stashId);
        result.metadata["scene.rebuild"] = "true";
        return result;
      });

  bus.registerHandler(
      "remove", CommandMetadata{"remove <path>", inverseFromMetadata(), true},
      [&scene, &editorState, state](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: remove <path>");
        }
        const auto selectionBeforeRemove = editorState.getSelected();
        const std::string stashId = allocateStashId(*state);
        CommandResult result =
            removeNodeToStash(scene, editorState, *state, args[0], stashId);
        if (!result.ok) {
          return result;
        }
        result.metadata["inverse.line"] =
            "__restore_from_stash " + quoteToken(stashId) + "\n" +
            buildSelectionCommand(selectionBeforeRemove);
        result.metadata["redo.line"] = "__remove_to_stash " +
                                       quoteToken(args[0]) + " " +
                                       quoteToken(stashId);
        return result;
      });

  bus.registerHandler(
      "copy", "copy <path>", [&scene, state](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: copy <path>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }
        if (node->isSceneRoot()) {
          return makeError("cannot copy scene root");
        }
        state->clipboard = captureNodeClipboardEntry(scene, *node);
        return makeOk("copied " + node->getPath(),
                      "{\"path\":\"" + jsonEscape(node->getPath()) + "\"}");
      });

  bus.registerHandler(
      "paste_as_sibling",
      CommandMetadata{"paste_as_sibling <targetPath>", inverseFromMetadata(),
                      true},
      [&scene, &editorState, state](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: paste_as_sibling <targetPath>");
        }
        if (!state->clipboard.has_value()) {
          return makeError("clipboard is empty");
        }

        SceneNode *target = nullptr;
        const CommandResult found = requireNode(scene, args[0], target);
        if (!found.ok) {
          return found;
        }
        const auto parent = target->getParent();
        if (!parent) {
          return makeError("cannot paste sibling for node without parent: " +
                           target->getPath());
        }

        const auto pasted = instantiateClipboardSubtree(
            scene, *state->clipboard, parent, true, true);
        registerSubtreeWithScene(scene, pasted);
        attachClipboardDirectionalLights(scene, *state->clipboard, pasted);
        editorState.select({pasted});

        const std::string pastedPath = pasted->getPath();
        const std::string stashId = allocateStashId(*state);
        CommandResult result =
            makeOk("pasted " + pastedPath,
                   "{\"path\":\"" + jsonEscape(pastedPath) + "\"}");
        result.metadata["inverse.line"] = "__remove_to_stash " +
                                          quoteToken(pastedPath) + " " +
                                          quoteToken(stashId);
        result.metadata["redo.line"] = "__restore_from_stash " +
                                       quoteToken(stashId) + "\nselect " +
                                       quoteToken(pastedPath);
        return result;
      });

  bus.registerHandler(
      "list", "list (nodes|cameras|lights)",
      [&scene](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: list (nodes|cameras|lights)");
        }

        if (args[0] == "nodes") {
          const std::string tree = scene.dumpTree();
          return makeOk(tree, "{\"tree\":\"" + jsonEscape(tree) + "\"}");
        }
        if (args[0] == "cameras") {
          std::ostringstream message;
          std::ostringstream structured;
          structured << "{\"paths\":[";
          const auto &cameras = scene.getCameras();
          for (usize i = 0; i < cameras.size(); ++i) {
            if (i != 0) {
              message << '\n';
              structured << ',';
            }
            const std::string path =
                cameras[i] ? cameras[i]->getPath() : std::string{};
            message << path;
            structured << '"' << jsonEscape(path) << '"';
          }
          structured << "]}";
          return makeOk(message.str(), structured.str());
        }
        if (args[0] == "lights") {
          const usize count = scene.getLights().size();
          return makeOk("lights: " + std::to_string(count),
                        "{\"count\":" + std::to_string(count) + "}");
        }

        return makeError("unknown list target: " + args[0]);
      });

  bus.registerHandler(
      "get", "get <path>.<field>",
      [&scene, materialContext](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: get <path>.<field>");
        }
        const auto split = splitFieldPath(args[0]);
        if (!split.has_value()) {
          return makeError("usage: get <path>.<field>");
        }
        std::string nodePath = split->first;
        std::string field = split->second;
        constexpr std::string_view kLightPathSuffix = ".light";
        if (nodePath.size() > kLightPathSuffix.size() &&
            nodePath.compare(nodePath.size() - kLightPathSuffix.size(),
                             kLightPathSuffix.size(), kLightPathSuffix) == 0) {
          nodePath.resize(nodePath.size() - kLightPathSuffix.size());
          field = "light." + field;
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, nodePath, node);
        if (!found.ok) {
          return found;
        }
        if (field == "materialUri" || field.rfind("nodeMaterial.", 0) == 0) {
          return getMaterialField(materialContext, nodePath, field);
        }
        return getField(*node, field);
      });

  bus.registerHandler(
      "set",
      CommandMetadata{"set <path>.<field> <value>", inverseFromMetadata(),
                      true},
      [&scene, materialContext](std::vector<std::string> args) {
        if (args.size() < 2) {
          return makeError("usage: set <path>.<field> <value>");
        }
        const auto split = splitFieldPath(args[0]);
        if (!split.has_value()) {
          return makeError("usage: set <path>.<field> <value>");
        }
        std::string nodePath = split->first;
        std::string field = split->second;
        constexpr std::string_view kLightPathSuffix = ".light";
        if (nodePath.size() > kLightPathSuffix.size() &&
            nodePath.compare(nodePath.size() - kLightPathSuffix.size(),
                             kLightPathSuffix.size(), kLightPathSuffix) == 0) {
          nodePath.resize(nodePath.size() - kLightPathSuffix.size());
          field = "light." + field;
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, nodePath, node);
        if (!found.ok) {
          return found;
        }
        const std::string oldName = node->getName();
        const bool materialField =
            field == "materialUri" || field.rfind("nodeMaterial.", 0) == 0;
        const std::string inverseLine =
            materialField ? buildMaterialSetInverseCommand(materialContext,
                                                           nodePath, field)
                          : buildSetInverseCommand(*node, field);
        CommandResult result =
            materialField
                ? setMaterialField(materialContext, nodePath, field, args, 1)
                : setField(*node, field, args, 1);
        if (result.ok && materialField) {
          result.metadata["scene.rebuild"] = "true";
        }
        if (result.ok && field == "name") {
          result.metadata["inverse.line"] =
              "set " + quoteToken(node->getPath() + ".name") + " " +
              quoteToken(oldName);
        } else if (result.ok && !inverseLine.empty()) {
          result.metadata["inverse.line"] = inverseLine;
        }
        return result;
      });

  bus.registerHandler(
      "clear",
      CommandMetadata{"clear <path>.nodeMaterial.<binding>.<member>",
                      inverseFromMetadata(), true},
      [&scene, materialContext](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError(
              "usage: clear <path>.nodeMaterial.<binding>.<member>");
        }
        const auto split = splitFieldPath(args[0]);
        if (!split.has_value()) {
          return makeError(
              "usage: clear <path>.nodeMaterial.<binding>.<member>");
        }
        const auto target = parseNodeMaterialTarget(split->second);
        if (!target.has_value()) {
          return makeError(
              "usage: clear <path>.nodeMaterial.<binding>.<member>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, split->first, node);
        if (!found.ok) {
          return found;
        }
        (void)node;
        if (!materialContext.clearNodeMaterialParameter) {
          return makeError("material editing unavailable: parameter callback "
                           "is not registered");
        }
        const std::string inverseLine = buildMaterialSetInverseCommand(
            materialContext, split->first, split->second);
        CommandResult result = materialContext.clearNodeMaterialParameter(
            split->first, target->binding, target->member);
        if (result.ok) {
          result.metadata["scene.rebuild"] = "true";
        }
        if (result.ok && !inverseLine.empty()) {
          result.metadata["inverse.line"] = inverseLine;
        }
        return result;
      });

  bus.registerHandler(
      "apply_material_override",
      CommandMetadata{"apply_material_override <path> baseColor",
                      inverseFromMetadata(), true},
      [&scene, materialContext](std::vector<std::string> args) {
        if (args.size() != 2) {
          return makeError("usage: apply_material_override <path> baseColor");
        }
        if (args[1] != "baseColor") {
          return makeError("unknown material override field: " + args[1]);
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }
        if (!materialContext.applyMaterialOverride) {
          return makeError(
              "material editing unavailable: apply callback is not registered");
        }
        const std::string inverseLine = buildMaterialSetInverseCommand(
            materialContext, args[0], "nodeMaterial.baseColor");
        CommandResult result =
            materialContext.applyMaterialOverride(args[0], args[1]);
        if (result.ok) {
          result.metadata["scene.rebuild"] = "true";
        }
        if (result.ok && !inverseLine.empty()) {
          result.metadata["inverse.line"] = inverseLine;
        }
        return result;
      });

  bus.registerHandler(
      "cam",
      CommandMetadata{
          "cam (control|look-at|reset|reset-editor-to-game|fov ...)",
          inverseFromMetadata(), true},
      [&scene, &editorState, sceneIoContext](std::vector<std::string> args) {
        if (args.empty()) {
          return makeError(
              "usage: cam (control|look-at|reset|reset-editor-to-game|fov "
              "...)");
        }
        if (args[0] == "control") {
          if (!sceneIoContext.cameraControl) {
            return makeError("camera control unavailable");
          }
          return sceneIoContext.cameraControl(args);
        }
        auto camera = findActiveCamera(scene, editorState);
        if (!camera.has_value()) {
          return makeError("no camera available");
        }

        if (args[0] == "reset") {
          camera->get().lookAt(Vec3f{0.0f, 0.0f, 3.0f}, Vec3f{0.0f, 0.0f, 0.0f},
                               Vec3f{0.0f, 1.0f, 0.0f});
          camera->get().updateMatrices();
          CommandResult result = makeOk("camera reset", "{\"mode\":\"reset\"}");
          result.metadata["editor_camera.resync"] = "true";
          return result;
        }
        if (args[0] == "reset-editor-to-game") {
          const SceneNodeSharedPtr editorNode = editorState.getEditorCamera();
          const SceneNodeSharedPtr gameNode = editorState.getPreviewCamera();
          if (!editorNode || !gameNode) {
            return makeError("editor or game camera is not configured");
          }
          auto editorCamera = editorNode->getComponent<CameraComponent>();
          auto gameCamera = gameNode->getComponent<CameraComponent>();
          if (!editorCamera.has_value() || !gameCamera.has_value()) {
            return makeError(
                "editor or game camera is missing a camera component");
          }
          editorCamera->get().lookAt(gameCamera->get().getEyePosition(),
                                     gameCamera->get().getLookTarget(),
                                     gameCamera->get().getUpVector());
          editorCamera->get().updateMatrices();
          CommandResult result = makeOk("editor camera reset from game camera",
                                        "{\"mode\":\"reset-editor-to-game\"}");
          result.metadata["editor_camera.resync"] = "true";
          return result;
        }
        if (args[0] == "fov") {
          if (args.size() != 2) {
            return makeError("usage: cam fov <degrees>");
          }
          const auto value = parseFloat(args[1]);
          if (!value) {
            return makeError("invalid float for cam fov");
          }
          CommandResult result;
          result.metadata["inverse.line"] =
              "cam fov " + formatFloat(camera->get().getFovY());
          camera->get().setFovY(*value);
          result.ok = true;
          result.message = "camera fov = " + formatFloat(*value);
          result.structured = "{\"value\":" + formatFloat(*value) + "}";
          result.metadata["editor_camera.resync"] = "true";
          return result;
        }
        if (args[0] == "look-at") {
          if (args.size() != 7) {
            return makeError("usage: cam look-at <eye-x> <eye-y> <eye-z> "
                             "<target-x> <target-y> <target-z>");
          }
          const auto eye = parseVec3(args, 1);
          const auto target = parseVec3(args, 4);
          if (!eye || !target) {
            return makeError("invalid float for cam look-at");
          }
          camera->get().lookAt(*eye, *target, Vec3f{0.0f, 1.0f, 0.0f});
          camera->get().updateMatrices();
          CommandResult result =
              makeOk("camera look-at updated",
                     "{\"eye\":" + makeVec3Json(*eye) +
                         ",\"target\":" + makeVec3Json(*target) + "}");
          result.metadata["editor_camera.resync"] = "true";
          return result;
        }

        return makeError("unknown cam action: " + args[0]);
      });

  bus.registerHandler(
      "preview",
      CommandMetadata{"preview (on|off|toggle)", inverseFromMetadata(), true},
      [&editorState, &scene](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: preview (on|off|toggle)");
        }
        const bool previewWasEnabled = editorState.isPreviewEnabled();
        if (args[0] == "on") {
          editorState.setPreviewEnabled(true);
        } else if (args[0] == "off") {
          editorState.setPreviewEnabled(false);
        } else if (args[0] == "toggle") {
          editorState.togglePreviewEnabled();
        } else {
          return makeError("unknown preview action: " + args[0]);
        }
        const SceneNodeSharedPtr activeCamera =
            editorState.syncActiveCamera(scene);
        CommandResult result =
            makeOk(std::string("preview ") +
                       (editorState.isPreviewEnabled() ? "on" : "off"),
                   std::string("{\"enabled\":") +
                       (editorState.isPreviewEnabled() ? "true" : "false") +
                       ",\"activePath\":\"" +
                       jsonEscape(activeCamera ? activeCamera->getPath()
                                               : std::string{}) +
                       "\"}");
        result.metadata["scene.rebuild"] = "true";
        result.metadata["inverse.line"] =
            std::string("preview ") + (previewWasEnabled ? "on" : "off");
        return result;
      });

  bus.registerHandler("undo", "undo", [&bus](std::vector<std::string> args) {
    if (!args.empty()) {
      return makeError("usage: undo");
    }
    return bus.undo();
  });

  bus.registerHandler("redo", "redo", [&bus](std::vector<std::string> args) {
    if (!args.empty()) {
      return makeError("usage: redo");
    }
    return bus.redo();
  });

  for (const std::string &verb : {"select", "move", "rotate", "scale"}) {
    for (usize argIndex = 0; argIndex < 8; ++argIndex) {
      bus.registerCompleter(verb, argIndex,
                            [&scene](const CompletionContext &context) {
                              return completeScenePaths(scene, context);
                            });
    }
  }
  bus.registerCompleter("set", 0, [&scene](const CompletionContext &context) {
    return completeSetTarget(scene, context);
  });
  bus.registerCompleter("get", 0, [&scene](const CompletionContext &context) {
    return completeSetTarget(scene, context);
  });
  bus.registerCompleter("add", 0, [](const CompletionContext &context) {
    return completeComponentTypes(context);
  });
  bus.registerCompleter("cam", 0, [](const CompletionContext &context) {
    return completeCamActions(context);
  });
}

} // namespace LX_core
