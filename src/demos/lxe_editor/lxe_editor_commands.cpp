#include "demos/lxe_editor/lxe_editor_commands.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include "scene_interaction_controller.hpp"
#include "scene_view_rect.hpp"
#include "ui_overlay.hpp"

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string jsonEscape(const std::string& text) {
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

[[nodiscard]] LX_core::CommandResult makeError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}};
}

[[nodiscard]] LX_core::CommandResult makeOk(std::string message,
                                            std::string structured = {}) {
  return LX_core::CommandResult{true, std::move(message), std::move(structured)};
}

[[nodiscard]] std::string makeVec3Json(const LX_core::Vec3f& value) {
  return std::string("{\"x\":") + formatFloat(value.x) + ",\"y\":" +
         formatFloat(value.y) + ",\"z\":" + formatFloat(value.z) + "}";
}

[[nodiscard]] std::string modeName(const UiOverlay::EditMode mode) {
  switch (mode) {
  case UiOverlay::EditMode::Selection:
    return "selection";
  case UiOverlay::EditMode::Orbit:
    return "orbit";
  case UiOverlay::EditMode::FreeFly:
    return "freefly";
  }
  return "selection";
}

[[nodiscard]] std::optional<UiOverlay::EditMode>
parseMode(const std::string& text) {
  if (text == "selection") {
    return UiOverlay::EditMode::Selection;
  }
  if (text == "orbit") {
    return UiOverlay::EditMode::Orbit;
  }
  if (text == "freefly") {
    return UiOverlay::EditMode::FreeFly;
  }
  return std::nullopt;
}

[[nodiscard]] int modeCode(const UiOverlay::EditMode mode) {
  return static_cast<int>(mode);
}

[[nodiscard]] UiOverlay::EditMode modeFromCode(const int code) {
  switch (code) {
  case static_cast<int>(UiOverlay::EditMode::Selection):
    return UiOverlay::EditMode::Selection;
  case static_cast<int>(UiOverlay::EditMode::Orbit):
    return UiOverlay::EditMode::Orbit;
  case static_cast<int>(UiOverlay::EditMode::FreeFly):
    return UiOverlay::EditMode::FreeFly;
  default:
    return UiOverlay::EditMode::Selection;
  }
}

[[nodiscard]] std::string
makeCameraJson(const LX_core::SceneNodeSharedPtr& node) {
  if (!node) {
    return "null";
  }
  const auto camera = node->getComponent<LX_core::CameraComponent>();
  if (!camera.has_value()) {
    return "{\"path\":\"" + jsonEscape(node->getPath()) +
           "\",\"error\":\"missing_camera_component\"}";
  }

  std::ostringstream oss;
  oss << "{\"path\":\"" << jsonEscape(node->getPath()) << "\""
      << ",\"eye\":" << makeVec3Json(camera->get().getEyePosition())
      << ",\"target\":" << makeVec3Json(camera->get().getLookTarget())
      << ",\"up\":" << makeVec3Json(camera->get().getUpVector())
      << ",\"fovY\":" << formatFloat(camera->get().fovY)
      << ",\"near\":" << formatFloat(camera->get().nearPlane)
      << ",\"far\":" << formatFloat(camera->get().farPlane)
      << ",\"aspect\":" << formatFloat(camera->get().aspect) << "}";
  return oss.str();
}

[[nodiscard]] std::string makeSelectionJson(
    LX_core::EditorState& editorState,
    SceneInteractionController& interaction) {
  std::ostringstream oss;
  oss << "{\"paths\":[";
  const auto selected = editorState.getSelected();
  for (usize i = 0; i < selected.size(); ++i) {
    if (i != 0) {
      oss << ',';
    }
    oss << '"' << jsonEscape(selected[i]->getPath()) << '"';
  }
  oss << "]";
  if (const auto primary = editorState.getPrimarySelected(); primary.has_value()) {
    oss << ",\"primaryPath\":\"" << jsonEscape(primary->get().getPath()) << '"';
    const auto bounds = primary->get().getWorldBounds();
    if (bounds.isValid()) {
      oss << ",\"aabb\":{\"min\":" << makeVec3Json(bounds.min)
          << ",\"max\":" << makeVec3Json(bounds.max) << "}";
    }
  } else {
    oss << ",\"primaryPath\":null";
  }
  if (const auto hitPoint = interaction.lastHitPoint(); hitPoint.has_value()) {
    oss << ",\"lastHitPoint\":" << makeVec3Json(*hitPoint);
  } else {
    oss << ",\"lastHitPoint\":null";
  }
  oss << "}";
  return oss.str();
}

[[nodiscard]] std::string
makeSummaryJson(const LxeEditorCommandContext& context) {
  const LX_core::SceneNodeSharedPtr activeCamera =
      context.editorState.resolveActiveCamera(context.scene);
  std::ostringstream oss;
  oss << "{\"sceneName\":\"" << jsonEscape(context.scene.getSceneName()) << "\""
      << ",\"dirty\":" << (context.dirty() ? "true" : "false")
      << ",\"permission\":\"" << jsonEscape(context.permission()) << "\""
      << ",\"previewEnabled\":"
      << (context.editorState.isPreviewEnabled() ? "true" : "false")
      << ",\"mode\":\"" << modeName(modeFromCode(context.getEditMode())) << "\""
      << ",\"selectionCount\":" << context.editorState.getSelected().size()
      << ",\"activeCameraPath\":";
  if (activeCamera) {
    oss << '"' << jsonEscape(activeCamera->getPath()) << '"';
  } else {
    oss << "null";
  }
  oss << ",\"documentPath\":";
  if (const auto path = context.currentDocumentPath(); path.has_value()) {
    oss << '"' << jsonEscape(*path) << '"';
  } else {
    oss << "null";
  }
  oss << ",\"sourceKind\":";
  if (const auto kind = context.currentSourceKind(); kind.has_value()) {
    oss << '"' << jsonEscape(*kind) << '"';
  } else {
    oss << "null";
  }
  oss << "}";
  return oss.str();
}

} // namespace

void registerLxeEditorCommands(
    LX_core::CommandBus& bus,
    const LxeEditorCommandContext& context) {
  auto* editorState = &context.editorState;
  auto* scene = &context.scene;
  auto* interaction = &context.interaction;
  auto getEditMode = context.getEditMode;
  auto setEditMode = context.setEditMode;
  auto sceneViewRect = context.sceneViewRect;
  auto dirty = context.dirty;
  auto permission = context.permission;
  auto currentDocumentPath = context.currentDocumentPath;
  auto currentSourceKind = context.currentSourceKind;
  auto persistedHistory = context.persistedHistory;

  bus.registerHandler(
      "quit", "quit",
      [](std::vector<std::string> args) {
        if (!args.empty()) {
          return makeError("usage: quit");
        }
        LX_core::CommandResult result =
            makeOk("quitting editor", "{\"quitting\":true}");
        result.metadata["editor.quit"] = "true";
        return result;
      });

  bus.registerHandler(
      "mode", "mode [selection|orbit|freefly|status]",
      [getEditMode, setEditMode](std::vector<std::string> args) {
        if (args.empty()) {
          const UiOverlay::EditMode mode = modeFromCode(getEditMode());
          return makeOk("mode " + modeName(mode),
                        "{\"mode\":\"" + modeName(mode) + "\"}");
        }
        if (args.size() != 1) {
          return makeError("usage: mode [selection|orbit|freefly|status]");
        }
        if (args[0] == "status") {
          const UiOverlay::EditMode mode = modeFromCode(getEditMode());
          return makeOk("mode " + modeName(mode),
                        "{\"mode\":\"" + modeName(mode) + "\"}");
        }
        const auto mode = parseMode(args[0]);
        if (!mode.has_value()) {
          return makeError("unknown mode: " + args[0]);
        }
        const UiOverlay::EditMode previousMode = modeFromCode(getEditMode());
        setEditMode(modeCode(*mode));
        LX_core::CommandResult result =
            makeOk("mode " + modeName(*mode),
                   "{\"mode\":\"" + modeName(*mode) + "\"}");
        result.metadata["inverse.line"] = "mode " + modeName(previousMode);
        return result;
      });

  bus.registerHandler(
      "state", "state (summary|selection|cameras|scene|toolbar|history)",
      [editorState, scene, interaction, getEditMode, dirty, permission,
       currentDocumentPath, currentSourceKind,
       persistedHistory](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeError(
              "usage: state (summary|selection|cameras|scene|toolbar|history)");
        }
        const LxeEditorCommandContext stateContext{
            .editorState = *editorState,
            .scene = *scene,
            .interaction = *interaction,
            .getEditMode = getEditMode,
            .setEditMode = [](int) {},
            .sceneViewRect = [] { return SceneViewRect{}; },
            .dirty = dirty,
            .permission = permission,
            .currentDocumentPath = currentDocumentPath,
            .currentSourceKind = currentSourceKind,
            .persistedHistory = persistedHistory,
        };
        if (args[0] == "summary") {
          const std::string structured = makeSummaryJson(stateContext);
          return makeOk(structured, structured);
        }
        if (args[0] == "selection") {
          const std::string structured =
              makeSelectionJson(*editorState, *interaction);
          return makeOk(structured, structured);
        }
        if (args[0] == "cameras") {
          const LX_core::SceneNodeSharedPtr activeCamera =
              editorState->resolveActiveCamera(*scene);
          const std::string structured =
              "{\"activePath\":" +
              std::string(activeCamera
                              ? "\"" + jsonEscape(activeCamera->getPath()) + "\""
                              : "null") +
              ",\"editor\":" +
              makeCameraJson(editorState->getEditorCamera()) +
              ",\"game\":" + makeCameraJson(editorState->getPreviewCamera()) +
              "}";
          return makeOk(structured, structured);
        }
        if (args[0] == "scene") {
          std::ostringstream oss;
          oss << "{\"sceneName\":\"" << jsonEscape(scene->getSceneName())
              << "\",\"rootPath\":\""
              << jsonEscape(scene->getRootNode()
                                ? scene->getRootNode()->getPath()
                                : std::string("/"))
              << "\",\"documentPath\":";
          if (const auto path = currentDocumentPath(); path.has_value()) {
            oss << '"' << jsonEscape(*path) << '"';
          } else {
            oss << "null";
          }
          oss << ",\"sourceKind\":";
          if (const auto kind = currentSourceKind(); kind.has_value()) {
            oss << '"' << jsonEscape(*kind) << '"';
          } else {
            oss << "null";
          }
          oss << ",\"dirty\":" << (dirty() ? "true" : "false")
              << ",\"permission\":\"" << jsonEscape(permission()) << "\""
              << ",\"nodeCount\":" << scene->listAllPaths().size()
              << ",\"cameraCount\":" << scene->getCameras().size()
              << ",\"lightCount\":" << scene->getLights().size() << "}";
          const std::string structured = oss.str();
          return makeOk(structured, structured);
        }
        if (args[0] == "toolbar") {
          const std::string structured = "{\"mode\":\"" +
                                         modeName(modeFromCode(getEditMode())) +
                                         "\",\"previewEnabled\":" +
                                         std::string(editorState->isPreviewEnabled()
                                                         ? "true"
                                                         : "false") +
                                         "}";
          return makeOk(structured, structured);
        }
        if (args[0] == "history") {
          const std::vector<std::string> lines =
              persistedHistory ? persistedHistory() : std::vector<std::string>{};
          std::ostringstream oss;
          oss << "{\"lines\":[";
          for (usize i = 0; i < lines.size(); ++i) {
            if (i != 0) {
              oss << ',';
            }
            oss << '"' << jsonEscape(lines[i]) << '"';
          }
          oss << "],\"count\":" << lines.size() << "}";
          const std::string structured = oss.str();
          return makeOk(structured, structured);
        }
        return makeError("unknown state target: " + args[0]);
      });

  bus.registerHandler(
      "pick", "pick <x> <y>",
      [editorState, interaction, sceneViewRect](std::vector<std::string> args) {
        if (args.size() != 2) {
          return makeError("usage: pick <x> <y>");
        }
        if (editorState->isPreviewEnabled()) {
          return makeError("pick unavailable while preview is enabled");
        }
        try {
          const float x = std::stof(args[0]);
          const float y = std::stof(args[1]);
          const LX_core::CommandResult result =
              interaction->dispatchPickingClick(LX_core::Vec2f{x, y},
                                                sceneViewRect());
          if (!result.ok) {
            return result;
          }
          const std::string structured =
              makeSelectionJson(*editorState, *interaction);
          return makeOk(result.message, structured);
        } catch (...) {
          return makeError("invalid float for pick");
        }
      });

  bus.registerCompleter(
      "mode", 0, [](const LX_core::CompletionContext& context) {
        static const std::vector<std::string> kModes = {"selection", "orbit",
                                                        "freefly"};
        std::vector<std::string> out;
        for (const auto& mode : kModes) {
          if (mode.rfind(context.partialToken, 0) == 0) {
            out.push_back(mode);
          }
        }
        return out;
      });
  bus.registerCompleter(
      "state", 0, [](const LX_core::CompletionContext& context) {
        static const std::vector<std::string> kTargets = {
            "summary", "selection", "cameras", "scene", "toolbar"};
        std::vector<std::string> out;
        for (const auto& target : kTargets) {
          if (target.rfind(context.partialToken, 0) == 0) {
            out.push_back(target);
          }
        }
        return out;
      });
}

} // namespace LX_demo::lxe_editor
