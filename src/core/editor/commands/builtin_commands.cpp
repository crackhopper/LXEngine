#include "core/editor/commands/builtin_commands.hpp"

#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

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

[[nodiscard]] std::optional<Vec3f> parseVec3(const std::vector<std::string> &args,
                                             const usize startIndex) {
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

[[nodiscard]] std::string buildSelectionCommand(
    const std::vector<SceneNodeSharedPtr> &selection) {
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

[[nodiscard]] std::string buildHelpMessage(CommandBus &bus,
                                           const std::vector<std::string> &args) {
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

[[nodiscard]] std::string makeVerbListJson(const std::vector<std::string> &verbs) {
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
  oss << "{\"x\":" << value.x << ",\"y\":" << value.y << ",\"z\":"
      << value.z << "}";
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

[[nodiscard]] std::string lowerCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return text;
}

[[nodiscard]] std::shared_ptr<DirectionalLight>
resolveDirectionalLight(SceneNode &node) {
  const auto scene = node.getAttachedScene();
  if (!scene) {
    return nullptr;
  }

  const std::string tag = lowerCopy(node.getName() + " " + node.getPath());
  if (tag.find("light") == std::string::npos) {
    return nullptr;
  }

  for (const auto &light : scene->getLights()) {
    const auto directionalLight = std::dynamic_pointer_cast<DirectionalLight>(light);
    if (directionalLight && directionalLight->ubo) {
      return directionalLight;
    }
  }
  return nullptr;
}

[[nodiscard]] std::vector<std::string> listComponentTypes() {
  return {"camera", "light", "mesh"};
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

[[nodiscard]] std::optional<std::pair<std::string, std::string>>
splitFieldPath(const std::string &text) {
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

[[nodiscard]] CommandResult getField(SceneNode &node, const std::string &field) {
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
    const float value = camera->get().fovY;
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
    return makeOk("near = " + formatFloat(camera->get().nearPlane),
                  "{\"value\":" + formatFloat(camera->get().nearPlane) + "}");
  }
  if (field == "far") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: far");
    }
    return makeOk("far = " + formatFloat(camera->get().farPlane),
                  "{\"value\":" + formatFloat(camera->get().farPlane) + "}");
  }
  if (field == "projection") {
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: projection");
    }
    const std::string value =
        camera->get().type == CameraType::Perspective ? "perspective"
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
  if (field == "direction") {
    const auto light = resolveDirectionalLight(node);
    if (!light) {
      return makeError("field not available on node: direction");
    }
    const Vec3f value = Vec3f{light->ubo->param.dir.x, light->ubo->param.dir.y, light->ubo->param.dir.z};
    return makeOk("direction = (" + formatFloat(value.x) + ", " +
                      formatFloat(value.y) + ", " + formatFloat(value.z) + ")",
                  "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "color") {
    const auto light = resolveDirectionalLight(node);
    if (!light) {
      return makeError("field not available on node: color");
    }
    const Vec3f value = Vec3f{light->ubo->param.color.x, light->ubo->param.color.y, light->ubo->param.color.z};
    return makeOk("color = (" + formatFloat(value.x) + ", " +
                      formatFloat(value.y) + ", " + formatFloat(value.z) + ")",
                  "{\"value\":" + makeVec3Json(value) + "}");
  }
  if (field == "intensity") {
    const auto light = resolveDirectionalLight(node);
    if (!light) {
      return makeError("field not available on node: intensity");
    }
    const float value = light->ubo->param.color.w;
    return makeOk("intensity = " + formatFloat(value),
                  "{\"value\":" + formatFloat(value) + "}");
  }
  if (field == "name") {
    const std::string value = node.getName();
    return makeOk("name = " + value,
                  "{\"value\":\"" + jsonEscape(value) + "\"}");
  }

  return makeError("unknown field: " + field);
}

[[nodiscard]] std::string buildSetInverseCommand(SceneNode &node,
                                                 const std::string &field) {
  const std::string path = node.getPath();
  if (field == "translation") {
    const Vec3f value = node.getTranslation();
    return "set " + quoteToken(path + ".translation") + " " + formatFloat(value.x) +
           " " + formatFloat(value.y) + " " + formatFloat(value.z);
  }
  if (field == "scale") {
    const Vec3f value = node.getScale();
    return "set " + quoteToken(path + ".scale") + " " + formatFloat(value.x) + " " +
           formatFloat(value.y) + " " + formatFloat(value.z);
  }
  if (field == "rotation") {
    const Vec3f value = quatToEulerDegrees(node.getRotation());
    return "set " + quoteToken(path + ".rotation") + " " + formatFloat(value.x) +
           " " + formatFloat(value.y) + " " + formatFloat(value.z);
  }
  if (field == "visibilityMask") {
    return "set " + quoteToken(path + ".visibilityMask") + " " +
           std::to_string(node.getVisibilityLayerMask());
  }
  if (field == "name") {
    return "set " + quoteToken(path + ".name") + " " + quoteToken(node.getName());
  }

  const auto camera = node.getComponent<CameraComponent>();
  if (field == "fov" && camera.has_value()) {
    return "set " + quoteToken(path + ".fov") + " " +
           formatFloat(camera->get().fovY);
  }
  if (field == "near" && camera.has_value()) {
    return "set " + quoteToken(path + ".near") + " " +
           formatFloat(camera->get().nearPlane);
  }
  if (field == "far" && camera.has_value()) {
    return "set " + quoteToken(path + ".far") + " " +
           formatFloat(camera->get().farPlane);
  }
  if (field == "projection" && camera.has_value()) {
    const std::string projection =
        camera->get().type == CameraType::Perspective ? "perspective"
                                                      : "orthographic";
    return "set " + quoteToken(path + ".projection") + " " + projection;
  }
  if (field == "cullingMask" && camera.has_value()) {
    return "set " + quoteToken(path + ".cullingMask") + " " +
           std::to_string(camera->get().getCullingMask());
  }

  const auto light = resolveDirectionalLight(node);
  if (field == "direction" && light) {
    return "set " + quoteToken(path + ".direction") + " " +
           formatFloat(light->ubo->param.dir.x) + " " +
           formatFloat(light->ubo->param.dir.y) + " " +
           formatFloat(light->ubo->param.dir.z);
  }
  if (field == "color" && light) {
    return "set " + quoteToken(path + ".color") + " " +
           formatFloat(light->ubo->param.color.x) + " " +
           formatFloat(light->ubo->param.color.y) + " " +
           formatFloat(light->ubo->param.color.z);
  }
  if (field == "intensity" && light) {
    return "set " + quoteToken(path + ".intensity") + " " +
           formatFloat(light->ubo->param.color.w);
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
  if (resolveDirectionalLight(node)) {
    fields.push_back("color");
    fields.push_back("direction");
    fields.push_back("intensity");
  }
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
  LightBaseSharedPtr light;
};

struct BuiltinCommandState {
  u64 nextStashId = 1;
  std::unordered_map<std::string, CommandNodeStashEntry> stash;
  std::unordered_map<const SceneNode *, LightBaseSharedPtr> attachedLights;
};

[[nodiscard]] std::string allocateStashId(BuiltinCommandState &state) {
  return std::to_string(state.nextStashId++);
}

[[nodiscard]] CommandResult removeNodeToStash(Scene &scene, EditorState &editorState,
                                              BuiltinCommandState &state,
                                              const std::string &path,
                                              const std::string &stashId) {
  SceneNode *node = nullptr;
  const CommandResult found = requireNode(scene, path, node);
  if (!found.ok) {
    return found;
  }

  const auto removed = node->shared_from_this();
  CommandNodeStashEntry entry;
  entry.node = removed;
  entry.parent = removed->getParent();

  const auto lightIt = state.attachedLights.find(removed.get());
  if (lightIt != state.attachedLights.end()) {
    entry.light = lightIt->second;
    scene.removeLight(lightIt->second);
    state.attachedLights.erase(lightIt);
  }

  scene.removeRenderable(removed);
  editorState.selectRemove(removed);
  state.stash[stashId] = std::move(entry);
  return makeOk("removed " + path,
                "{\"path\":\"" + jsonEscape(path) + "\",\"stash\":\"" +
                    jsonEscape(stashId) + "\"}");
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

  if (entry.node->getComponent<CameraComponent>().has_value()) {
    scene.addCamera(entry.node);
  } else {
    scene.addRenderable(entry.node);
  }

  if (entry.light) {
    scene.addLight(entry.light);
    state.attachedLights[entry.node.get()] = entry.light;
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
    return makeOk("translation updated", "{\"value\":" + makeVec3Json(*value) + "}");
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
      return makeError("usage: set <path>.scale <s> | set <path>.scale <sx> <sy> <sz>");
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
    return makeOk("rotation updated", "{\"value\":" + makeQuatJson(rotation) + "}");
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
    camera->get().fovY = *value;
    camera->get().updateMatrices();
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
    camera->get().nearPlane = *value;
    camera->get().updateMatrices();
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
    camera->get().farPlane = *value;
    camera->get().updateMatrices();
    return makeOk("far updated", "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "projection") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.projection <perspective|orthographic>");
    }
    const auto camera = node.getComponent<CameraComponent>();
    if (!camera.has_value()) {
      return makeError("field not available on node: projection");
    }
    const std::string value = lowerCopy(args[valueStartIndex]);
    if (value == "perspective") {
      camera->get().type = CameraType::Perspective;
    } else if (value == "orthographic") {
      camera->get().type = CameraType::Orthographic;
    } else {
      return makeError("invalid projection for set projection");
    }
    camera->get().updateMatrices();
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
  if (field == "direction") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.direction <x> <y> <z>");
    }
    const auto light = resolveDirectionalLight(node);
    if (!light) {
      return makeError("field not available on node: direction");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set direction");
    }
    light->ubo->param.dir = Vec4f{value->x, value->y, value->z, 0.0f};
    light->ubo->setDirty();
    return makeOk("direction updated", "{\"value\":" + makeVec3Json(*value) + "}");
  }
  if (field == "color") {
    if (args.size() != valueStartIndex + 3) {
      return makeError("usage: set <path>.color <r> <g> <b>");
    }
    const auto light = resolveDirectionalLight(node);
    if (!light) {
      return makeError("field not available on node: color");
    }
    const auto value = parseVec3(args, valueStartIndex);
    if (!value) {
      return makeError("invalid float for set color");
    }
    light->ubo->param.color =
        Vec4f{value->x, value->y, value->z, light->ubo->param.color.w};
    light->ubo->setDirty();
    return makeOk("color updated", "{\"value\":" + makeVec3Json(*value) + "}");
  }
  if (field == "intensity") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.intensity <value>");
    }
    const auto light = resolveDirectionalLight(node);
    if (!light) {
      return makeError("field not available on node: intensity");
    }
    const auto value = parseFloat(args[valueStartIndex]);
    if (!value) {
      return makeError("invalid float for set intensity");
    }
    light->ubo->param.color.w = *value;
    light->ubo->setDirty();
    return makeOk("intensity updated", "{\"value\":" + formatFloat(*value) + "}");
  }
  if (field == "name") {
    if (args.size() != valueStartIndex + 1) {
      return makeError("usage: set <path>.name <value>");
    }
    node.setName(args[valueStartIndex]);
    return makeOk("name updated", "{\"value\":\"" +
                                      jsonEscape(node.getName()) + "\"}");
  }

  return makeError("unknown field: " + field);
}

[[nodiscard]] InverseFn inverseFromMetadata() {
  return [](const ParsedCommand &, const CommandResult &result)
             -> std::optional<std::string> {
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
  const auto sceneLoad = sceneIoContext.load;
  const auto sceneSave = sceneIoContext.save;

  bus.registerHandler(
      "help", "help [verb]",
      [&bus](std::vector<std::string> args) {
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
      "scene", "scene load <path> | scene save [path]",
      [sceneLoad, sceneSave](std::vector<std::string> args) {
        if (args.empty()) {
          return makeError("usage: scene load <path> | scene save [path]");
        }

        const std::string &action = args[0];
        if (action == "load") {
          if (args.size() != 2) {
            return makeError("usage: scene load <path>");
          }
          if (!sceneLoad) {
            return makeSceneIoUnavailable("load");
          }
          return markClearsHistoryOnSuccess(sceneLoad(args[1]));
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

  bus.registerHandler(
      "__remove_to_stash", "__remove_to_stash <path> <stash-id>",
      [&scene, &editorState, state](std::vector<std::string> args) {
        if (args.size() != 2) {
          return makeError("usage: __remove_to_stash <path> <stash-id>");
        }
        return removeNodeToStash(scene, editorState, *state, args[0], args[1]);
      });

  bus.registerHandler(
      "__restore_from_stash", "__restore_from_stash <stash-id>",
      [&scene, state](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: __restore_from_stash <stash-id>");
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
        result.message = "selected " + std::to_string(nodes->size()) + " node(s)";
        result.structured = structured.str();
        return result;
      });

  bus.registerHandler(
      "deselect",
      CommandMetadata{"deselect", inverseFromMetadata(), true},
      [&editorState](std::vector<std::string> args) {
        if (!args.empty()) {
          return makeError("usage: deselect");
        }
        CommandResult result = makeOk("selection cleared", "{\"selected\":null}");
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
            args, error, "usage: move <path> <x> <y> <z> | move <path...> <dx> <dy> <dz>",
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
          CommandResult result =
              makeOk("moved " + (*nodes)[0]->getPath() + " to (" +
                         formatFloat(value->x) + ", " + formatFloat(value->y) + ", " +
                         formatFloat(value->z) + ")",
                     makeVec3Json(*value));
          result.metadata["inverse.line"] = inverseLines.front();
          return result;
        }

        for (const auto &node : *nodes) {
          node->setTranslation(node->getTranslation() + *value);
        }

        CommandResult result =
            makeOk("moved " + std::to_string(nodes->size()) + " node(s) by delta (" +
                       formatFloat(value->x) + ", " + formatFloat(value->y) + ", " +
                       formatFloat(value->z) + ")",
                   makeVec3Json(*value));
        result.metadata["inverse.line"] = joinLines(inverseLines);
        return result;
      });

  bus.registerHandler(
      "rotate",
      CommandMetadata{"rotate <path> <rx-deg> <ry-deg> <rz-deg> | rotate <path...> <rx-deg> <ry-deg> <rz-deg>",
                      inverseFromMetadata(), true},
      [&scene](std::vector<std::string> args) {
        CommandResult error;
        const auto paths = extractTargetPathsAndVec3Args(
            args, error,
            "usage: rotate <path> <rx-deg> <ry-deg> <rz-deg> | rotate <path...> <rx-deg> <ry-deg> <rz-deg>",
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
        }

        CommandResult result = makeOk(
            nodes->size() == 1 ? "rotated " + (*nodes)[0]->getPath()
                               : "rotated " + std::to_string(nodes->size()) +
                                     " node(s) by delta",
            makeQuatJson(rotation));
        result.metadata["inverse.line"] =
            nodes->size() == 1 ? inverseLines.front() : joinLines(inverseLines);
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
      "add", CommandMetadata{"add (mesh|light|camera) <name> [parentPath]",
                              inverseFromMetadata(), true},
      [&scene, &editorState, state](std::vector<std::string> args) {
        if (args.size() != 2 && args.size() != 3) {
          return makeError("usage: add (mesh|light|camera) <name> [parentPath]");
        }

        const std::string &kind = args[0];
        const std::string &name = args[1];
        auto node = SceneNode::create(kind + "_node");
        node->setName(name);
        SceneNodeSharedPtr parent;
        if (args.size() == 3) {
          SceneNode *parentNode = nullptr;
          const CommandResult found = requireNode(scene, args[2], parentNode);
          if (!found.ok) {
            return found;
          }
          parent = parentNode->shared_from_this();
        } else {
          parent = chooseCommandParent(editorState);
        }
        if (parent) {
          node->setParent(parent);
        }
        const std::string stashId = allocateStashId(*state);

        if (kind == "mesh") {
          scene.addRenderable(node);
          CommandResult result = makeOk(
              "added mesh node " + node->getPath(),
              "{\"path\":\"" + jsonEscape(node->getPath()) + "\",\"kind\":\"mesh\"}");
          result.metadata["inverse.line"] =
              "__remove_to_stash " + quoteToken(node->getPath()) + " " + quoteToken(stashId);
          result.metadata["redo.line"] =
              "__restore_from_stash " + quoteToken(stashId);
          return result;
        }
        if (kind == "camera") {
          const auto camera = node->addComponent<CameraComponent>();
          if (!camera.has_value()) {
            return makeError("failed to add camera component");
          }
          camera->get().updateMatrices();
          scene.addCamera(node);
          CommandResult result = makeOk(
              "added camera " + node->getPath(),
              "{\"path\":\"" + jsonEscape(node->getPath()) + "\",\"kind\":\"camera\"}");
          result.metadata["inverse.line"] =
              "__remove_to_stash " + quoteToken(node->getPath()) + " " + quoteToken(stashId);
          result.metadata["redo.line"] =
              "__restore_from_stash " + quoteToken(stashId);
          return result;
        }
        if (kind == "light") {
          scene.addRenderable(node);
          const auto light = std::make_shared<DirectionalLight>();
          scene.addLight(light);
          state->attachedLights[node.get()] = light;
          CommandResult result =
              makeOk("added light placeholder " + node->getPath(),
                     "{\"path\":\"" + jsonEscape(node->getPath()) + "\",\"kind\":\"light\"}");
          result.metadata["inverse.line"] =
              "__remove_to_stash " + quoteToken(node->getPath()) + " " + quoteToken(stashId);
          result.metadata["redo.line"] =
              "__restore_from_stash " + quoteToken(stashId);
          return result;
        }

        return makeError("unknown add target: " + kind);
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
        result.metadata["redo.line"] =
            "__remove_to_stash " + quoteToken(args[0]) + " " + quoteToken(stashId);
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
            const std::string path = cameras[i] ? cameras[i]->getPath() : std::string{};
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
      [&scene](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: get <path>.<field>");
        }
        const auto split = splitFieldPath(args[0]);
        if (!split.has_value()) {
          return makeError("usage: get <path>.<field>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, split->first, node);
        if (!found.ok) {
          return found;
        }
        return getField(*node, split->second);
      });

  bus.registerHandler(
      "set", CommandMetadata{"set <path>.<field> <value>", inverseFromMetadata(),
                              true},
      [&scene](std::vector<std::string> args) {
        if (args.size() < 2) {
          return makeError("usage: set <path>.<field> <value>");
        }
        const auto split = splitFieldPath(args[0]);
        if (!split.has_value()) {
          return makeError("usage: set <path>.<field> <value>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, split->first, node);
        if (!found.ok) {
          return found;
        }
        const std::string oldName = node->getName();
        const std::string inverseLine = buildSetInverseCommand(*node, split->second);
        CommandResult result = setField(*node, split->second, args, 1);
        if (result.ok && split->second == "name") {
          result.metadata["inverse.line"] =
              "set " + quoteToken(node->getPath() + ".name") + " " +
              quoteToken(oldName);
        } else if (result.ok && !inverseLine.empty()) {
          result.metadata["inverse.line"] = inverseLine;
        }
        return result;
      });

  bus.registerHandler(
      "cam", CommandMetadata{"cam (look-at|reset|fov ...)", inverseFromMetadata(),
                              true},
      [&scene, &editorState](std::vector<std::string> args) {
        if (args.empty()) {
          return makeError("usage: cam (look-at|reset|fov ...)");
        }
        auto camera = findActiveCamera(scene, editorState);
        if (!camera.has_value()) {
          return makeError("no camera available");
        }

        if (args[0] == "reset") {
          camera->get().lookAt(Vec3f{0.0f, 0.0f, 3.0f}, Vec3f{0.0f, 0.0f, 0.0f},
                               Vec3f{0.0f, 1.0f, 0.0f});
          camera->get().updateMatrices();
          return makeOk("camera reset", "{\"mode\":\"reset\"}");
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
              "cam fov " + formatFloat(camera->get().fovY);
          camera->get().fovY = *value;
          camera->get().updateMatrices();
          result.ok = true;
          result.message = "camera fov = " + formatFloat(*value);
          result.structured = "{\"value\":" + formatFloat(*value) + "}";
          return result;
        }
        if (args[0] == "look-at") {
          if (args.size() != 7) {
            return makeError(
                "usage: cam look-at <eye-x> <eye-y> <eye-z> <target-x> <target-y> <target-z>");
          }
          const auto eye = parseVec3(args, 1);
          const auto target = parseVec3(args, 4);
          if (!eye || !target) {
            return makeError("invalid float for cam look-at");
          }
          camera->get().lookAt(*eye, *target, Vec3f{0.0f, 1.0f, 0.0f});
          camera->get().updateMatrices();
          return makeOk("camera look-at updated",
                        "{\"eye\":" + makeVec3Json(*eye) + ",\"target\":" +
                            makeVec3Json(*target) + "}");
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
        const SceneNodeSharedPtr activeCamera = editorState.syncActiveCamera(scene);
        CommandResult result =
            makeOk(std::string("preview ") +
                       (editorState.isPreviewEnabled() ? "on" : "off"),
                   std::string("{\"enabled\":") +
                       (editorState.isPreviewEnabled() ? "true" : "false") +
                       ",\"activePath\":\"" +
                       jsonEscape(activeCamera ? activeCamera->getPath() : std::string{}) +
                       "\"}");
        result.metadata["inverse.line"] =
            std::string("preview ") + (previewWasEnabled ? "on" : "off");
        return result;
      });

  bus.registerHandler(
      "undo", "undo",
      [&bus](std::vector<std::string> args) {
        if (!args.empty()) {
          return makeError("usage: undo");
        }
        return bus.undo();
      });

  bus.registerHandler(
      "redo", "redo",
      [&bus](std::vector<std::string> args) {
        if (!args.empty()) {
          return makeError("usage: redo");
        }
        return bus.redo();
      });

  for (const std::string &verb : {"select", "move", "rotate", "scale"}) {
    for (usize argIndex = 0; argIndex < 8; ++argIndex) {
      bus.registerCompleter(
          verb, argIndex, [&scene](const CompletionContext &context) {
            return completeScenePaths(scene, context);
          });
    }
  }
  bus.registerCompleter(
      "set", 0, [&scene](const CompletionContext &context) {
        return completeSetTarget(scene, context);
      });
  bus.registerCompleter(
      "get", 0, [&scene](const CompletionContext &context) {
        return completeSetTarget(scene, context);
      });
  bus.registerCompleter(
      "add", 0, [](const CompletionContext &context) {
        return completeComponentTypes(context);
      });
}

} // namespace LX_core
