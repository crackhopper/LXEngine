#include "core/editor/commands/builtin_commands.hpp"

#include "core/editor/editor_state.hpp"
#include "core/math/quat.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/light.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

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

[[nodiscard]] CommandResult requireNode(Scene &scene, const std::string &path,
                                        SceneNode *&outNode) {
  SceneNode *node = scene.findByPath(path);
  if (!node) {
    return makeError("node not found: " + path);
  }
  outNode = node;
  return CommandResult{true, {}, {}};
}

[[nodiscard]] SceneNodeSharedPtr chooseCommandParent(EditorState &editorState) {
  return editorState.getSelected();
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
  if (const auto selected = editorState.getSelected()) {
    auto selectedCamera = selected->getComponent<CameraComponent>();
    if (selectedCamera.has_value()) {
      return std::ref(selectedCamera->get());
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
  if (field == "name") {
    const std::string value = node.getName();
    return makeOk("name = " + value,
                  "{\"value\":\"" + jsonEscape(value) + "\"}");
  }

  return makeError("unknown field: " + field);
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

} // namespace

void registerBuiltinCommands(CommandBus &bus, EditorState &editorState,
                             Scene &scene) {
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
      "select", "select <path>",
      [&editorState, &scene](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: select <path>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }
        editorState.select(node->shared_from_this());
        return makeOk("selected " + node->getPath(),
                      "{\"path\":\"" + jsonEscape(node->getPath()) + "\"}");
      });

  bus.registerHandler(
      "deselect", "deselect",
      [&editorState](std::vector<std::string> args) {
        if (!args.empty()) {
          return makeError("usage: deselect");
        }
        editorState.deselect();
        return makeOk("selection cleared", "{\"selected\":null}");
      });

  bus.registerHandler(
      "move", "move <path> <x> <y> <z>",
      [&scene](std::vector<std::string> args) {
        if (args.size() != 4) {
          return makeError("usage: move <path> <x> <y> <z>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }
        const auto value = parseVec3(args, 1);
        if (!value) {
          return makeError("invalid float for move");
        }
        node->setTranslation(*value);
        return makeOk("moved " + node->getPath() + " to (" + formatFloat(value->x) +
                          ", " + formatFloat(value->y) + ", " +
                          formatFloat(value->z) + ")",
                      makeVec3Json(*value));
      });

  bus.registerHandler(
      "rotate", "rotate <path> <rx-deg> <ry-deg> <rz-deg>",
      [&scene](std::vector<std::string> args) {
        if (args.size() != 4) {
          return makeError("usage: rotate <path> <rx-deg> <ry-deg> <rz-deg>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }
        const auto value = parseVec3(args, 1);
        if (!value) {
          return makeError("invalid float for rotate");
        }
        const Quatf rotation = eulerDegreesToQuat(value->x, value->y, value->z);
        node->setRotation(rotation);
        return makeOk("rotated " + node->getPath(), makeQuatJson(rotation));
      });

  bus.registerHandler(
      "scale", "scale <path> <sx> <sy> <sz>",
      [&scene](std::vector<std::string> args) {
        if (args.size() != 2 && args.size() != 4) {
          return makeError("usage: scale <path> <s> | scale <path> <sx> <sy> <sz>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }

        Vec3f scale{};
        if (args.size() == 2) {
          const auto s = parseFloat(args[1]);
          if (!s) {
            return makeError("invalid float for scale");
          }
          scale = Vec3f{*s, *s, *s};
        } else {
          const auto parsed = parseVec3(args, 1);
          if (!parsed) {
            return makeError("invalid float for scale");
          }
          scale = *parsed;
        }

        node->setScale(scale);
        return makeOk("scaled " + node->getPath(), makeVec3Json(scale));
      });

  bus.registerHandler(
      "add", "add (mesh|light|camera) <name>",
      [&scene, &editorState](std::vector<std::string> args) {
        if (args.size() != 2) {
          return makeError("usage: add (mesh|light|camera) <name>");
        }

        const std::string &kind = args[0];
        const std::string &name = args[1];
        auto node = SceneNode::create(kind + "_node");
        node->setName(name);
        if (const auto parent = chooseCommandParent(editorState)) {
          node->setParent(parent);
        }

        if (kind == "mesh") {
          scene.addRenderable(node);
          return makeOk("added mesh node " + node->getPath(),
                        "{\"path\":\"" + jsonEscape(node->getPath()) +
                            "\",\"kind\":\"mesh\"}");
        }
        if (kind == "camera") {
          const auto camera = node->addComponent<CameraComponent>();
          if (!camera.has_value()) {
            return makeError("failed to add camera component");
          }
          camera->get().updateMatrices();
          scene.addCamera(node);
          return makeOk("added camera " + node->getPath(),
                        "{\"path\":\"" + jsonEscape(node->getPath()) +
                            "\",\"kind\":\"camera\"}");
        }
        if (kind == "light") {
          scene.addRenderable(node);
          scene.addLight(std::make_shared<DirectionalLight>());
          return makeOk("added light placeholder " + node->getPath(),
                        "{\"path\":\"" + jsonEscape(node->getPath()) +
                            "\",\"kind\":\"light\"}");
        }

        return makeError("unknown add target: " + kind);
      });

  bus.registerHandler(
      "remove", "remove <path>",
      [&scene, &editorState](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: remove <path>");
        }
        SceneNode *node = nullptr;
        const CommandResult found = requireNode(scene, args[0], node);
        if (!found.ok) {
          return found;
        }
        const auto removed = node->shared_from_this();
        scene.removeRenderable(removed);
        if (const auto selected = editorState.getSelected();
            selected && selected.get() == removed.get()) {
          editorState.deselect();
        }
        return makeOk("removed " + args[0],
                      "{\"path\":\"" + jsonEscape(args[0]) + "\"}");
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
      "set", "set <path>.<field> <value>",
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
        return setField(*node, split->second, args, 1);
      });

  bus.registerHandler(
      "cam", "cam (look-at|reset|fov ...)",
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
          camera->get().fovY = *value;
          camera->get().updateMatrices();
          return makeOk("camera fov = " + formatFloat(*value),
                        "{\"value\":" + formatFloat(*value) + "}");
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
      "preview", "preview (on|off|toggle)",
      [&editorState](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError("usage: preview (on|off|toggle)");
        }
        if (args[0] == "on") {
          editorState.setPreviewEnabled(true);
        } else if (args[0] == "off") {
          editorState.setPreviewEnabled(false);
        } else if (args[0] == "toggle") {
          editorState.togglePreviewEnabled();
        } else {
          return makeError("unknown preview action: " + args[0]);
        }
        return makeOk(std::string("preview ") +
                          (editorState.isPreviewEnabled() ? "on" : "off"),
                      std::string("{\"enabled\":") +
                          (editorState.isPreviewEnabled() ? "true}" : "false}"));
      });
}

} // namespace LX_core
