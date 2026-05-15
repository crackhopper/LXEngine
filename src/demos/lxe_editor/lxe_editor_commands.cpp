#include "demos/lxe_editor/lxe_editor_commands.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"

#include "scene_interaction_controller.hpp"
#include "scene_view_rect.hpp"
#include "ui_overlay.hpp"

#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace LX_demo::lxe_editor {
namespace {

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

[[nodiscard]] std::string_view trimView(std::string_view text) {
  while (!text.empty() &&
         (text.front() == ' ' || text.front() == '\t' ||
          text.front() == '\r' || text.front() == '\n')) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         (text.back() == ' ' || text.back() == '\t' ||
          text.back() == '\r' || text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string sanitizeProjectSummaryJson(std::string text) {
  const std::string_view trimmed = trimView(text);
  if (trimmed.empty() || trimmed == "null") {
    return "null";
  }
  if (trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}') {
    return std::string(trimmed);
  }
  return "null";
}

[[nodiscard]] LX_core::CommandResult makeError(std::string message) {
  return LX_core::CommandResult{false, std::move(message), {}};
}

[[nodiscard]] LX_core::CommandResult makeOk(std::string message,
                                            std::string structured = {}) {
  return LX_core::CommandResult{true, std::move(message),
                                std::move(structured)};
}

[[nodiscard]] LX_core::CommandResult
makeDisplayResult(std::string successMessage, std::string structured) {
  if (structured.find("\"ok\":false") != std::string::npos) {
    return LX_core::CommandResult{false, "display command failed",
                                  std::move(structured)};
  }
  return makeOk(std::move(successMessage), std::move(structured));
}

[[nodiscard]] std::string makeVec3Json(const LX_core::Vec3f &value) {
  return std::string("{\"x\":") + formatFloat(value.x) +
         ",\"y\":" + formatFloat(value.y) + ",\"z\":" + formatFloat(value.z) +
         "}";
}

[[nodiscard]] std::string modeName(const UiOverlay::EditorMode mode) {
  switch (mode) {
  case UiOverlay::EditorMode::Selection:
    return "selection";
  }
  return "selection";
}

[[nodiscard]] std::optional<UiOverlay::EditorMode>
parseMode(const std::string &text) {
  if (text == "selection") {
    return UiOverlay::EditorMode::Selection;
  }
  return std::nullopt;
}

[[nodiscard]] int modeCode(const UiOverlay::EditorMode mode) {
  return static_cast<int>(mode);
}

[[nodiscard]] UiOverlay::EditorMode modeFromCode(const int code) {
  switch (code) {
  case static_cast<int>(UiOverlay::EditorMode::Selection):
    return UiOverlay::EditorMode::Selection;
  default:
    return UiOverlay::EditorMode::Selection;
  }
}

[[nodiscard]] std::string
cameraControlModeName(const UiOverlay::CameraControlMode mode) {
  switch (mode) {
  case UiOverlay::CameraControlMode::Orbit:
    return "orbit";
  case UiOverlay::CameraControlMode::FreeFly:
    return "freefly";
  }
  return "orbit";
}

[[nodiscard]] UiOverlay::CameraControlMode
cameraControlModeFromCode(const int code) {
  switch (code) {
  case static_cast<int>(UiOverlay::CameraControlMode::Orbit):
    return UiOverlay::CameraControlMode::Orbit;
  case static_cast<int>(UiOverlay::CameraControlMode::FreeFly):
    return UiOverlay::CameraControlMode::FreeFly;
  default:
    return UiOverlay::CameraControlMode::Orbit;
  }
}

[[nodiscard]] std::string
makeCameraJson(const LX_core::SceneNodeSharedPtr &node) {
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
      << ",\"fovY\":" << formatFloat(camera->get().getFovY())
      << ",\"near\":" << formatFloat(camera->get().getNearPlane())
      << ",\"far\":" << formatFloat(camera->get().getFarPlane())
      << ",\"aspect\":" << formatFloat(camera->get().getAspect()) << "}";
  return oss.str();
}

[[nodiscard]] std::string
makeSelectionJson(LX_core::EditorState &editorState,
                  SceneInteractionController &interaction) {
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
  if (const auto primary = editorState.getPrimarySelected();
      primary.has_value()) {
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
makeSummaryJson(const LxeEditorCommandContext &context) {
  const LX_core::SceneNodeSharedPtr activeCamera =
      context.editorState.resolveActiveCamera(context.scene);
  const std::string projectSummary =
      sanitizeProjectSummaryJson(context.projectSummaryJson
                                     ? context.projectSummaryJson()
                                     : std::string{});
  std::ostringstream oss;
  oss << "{\"sceneName\":\"" << jsonEscape(context.scene.getSceneName()) << "\""
      << ",\"dirty\":" << (context.dirty() ? "true" : "false")
      << ",\"permission\":\"" << jsonEscape(context.permission()) << "\""
      << ",\"previewEnabled\":"
      << (context.editorState.isPreviewEnabled() ? "true" : "false")
      << ",\"debugEnabled\":"
      << (context.debugEnabled && context.debugEnabled() ? "true" : "false")
      << ",\"mode\":\"" << modeName(modeFromCode(context.getEditMode())) << "\""
      << ",\"camera\":\""
      << cameraControlModeName(
             cameraControlModeFromCode(context.getCameraControlMode()))
      << "\""
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
  oss << ",\"project\":" << (projectSummary.empty() ? "null" : projectSummary)
      << "}";
  return oss.str();
}

[[nodiscard]] std::string
makeRecordingStatusJson(const RecordingStatus &status) {
  std::ostringstream oss;
  oss << "{\"enabled\":" << (status.enabled ? "true" : "false")
      << ",\"active\":" << (status.active ? "true" : "false")
      << ",\"sessionId\":\"" << jsonEscape(status.sessionId) << "\""
      << ",\"detailLevel\":\"" << recordingDetailLevelName(status.detailLevel)
      << "\"" << ",\"stepCount\":" << status.stepCount << ",\"lastSavedPath\":";
  if (status.lastSavedPath.has_value()) {
    oss << '"' << jsonEscape(status.lastSavedPath->string()) << '"';
  } else {
    oss << "null";
  }
  oss << "}";
  return oss.str();
}

[[nodiscard]] std::optional<RecordingDetailLevel>
parseRecordingDetailLevel(const std::vector<std::string> &args,
                          const usize index) {
  if (index >= args.size()) {
    return RecordingDetailLevel::Basic;
  }
  return recordingDetailLevelFromName(args[index]);
}

[[nodiscard]] std::string joinArgs(const std::vector<std::string> &args,
                                   const usize first) {
  std::string out;
  for (usize i = first; i < args.size(); ++i) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += args[i];
  }
  return out;
}

[[nodiscard]] LX_core::CommandResult
makeDisplayHookError(const std::exception &error) {
  return makeError(std::string("display error: ") + error.what());
}

} // namespace

void registerLxeEditorCommands(LX_core::CommandBus &bus,
                               const LxeEditorCommandContext &context) {
  auto *editorState = &context.editorState;
  auto *scene = &context.scene;
  auto *interaction = &context.interaction;
  auto getEditMode = context.getEditMode;
  auto setEditMode = context.setEditMode;
  auto getCameraControlMode = context.getCameraControlMode;
  auto sceneViewRect = context.sceneViewRect;
  auto dirty = context.dirty;
  auto permission = context.permission;
  auto debugEnabled = context.debugEnabled;
  auto setDebugEnabled = context.setDebugEnabled;
  auto currentDocumentPath = context.currentDocumentPath;
  auto projectCommand = context.projectCommand;
  auto sceneCommand = context.sceneCommand;
  auto projectSummaryJson = context.projectSummaryJson;
  auto persistedHistory = context.persistedHistory;
  auto recording = context.recording;
  auto buildInfoJson = context.buildInfoJson;
  auto displayListJson = context.displayListJson;
  auto displayActiveJson = context.displayActiveJson;
  auto displayConfigGetJson = context.displayConfigGetJson;
  auto displayConfigSet = context.displayConfigSet;
  auto displaySelect = context.displaySelect;
  interaction->setDebugLoggingHooks(context.debugEnabled,
                                    context.appendConsoleDebugLine);

  bus.registerHandler("quit", "quit", [](std::vector<std::string> args) {
    if (!args.empty()) {
      return makeError("usage: quit");
    }
    LX_core::CommandResult result =
        makeOk("quitting editor", "{\"quitting\":true}");
    result.metadata["editor.quit"] = "true";
    return result;
  });

  bus.registerHandler(
      "display",
      "display list|active|config get <key|active|default>|config set "
      "<key|default> <json-or-yaml-patch>|select <key>",
      [displayListJson, displayActiveJson, displayConfigGetJson,
       displayConfigSet, displaySelect](std::vector<std::string> args) {
        if (args.size() == 1 && args[0] == "list") {
          if (!displayListJson) {
            return makeError("display list unavailable");
          }
          try {
            std::string structured = displayListJson();
            std::string message = structured;
            return makeDisplayResult(std::move(message), std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 1 && args[0] == "active") {
          if (!displayActiveJson) {
            return makeError("display active unavailable");
          }
          try {
            std::string structured = displayActiveJson();
            std::string message = structured;
            return makeDisplayResult(std::move(message), std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 3 && args[0] == "config" && args[1] == "get") {
          if (!displayConfigGetJson) {
            return makeError("display config get unavailable");
          }
          try {
            std::string structured = displayConfigGetJson(args[2]);
            std::string message = structured;
            return makeDisplayResult(std::move(message), std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() >= 4 && args[0] == "config" && args[1] == "set") {
          if (!displayConfigSet) {
            return makeError("display config set unavailable");
          }
          try {
            const std::string patch = joinArgs(args, 3);
            std::string structured = displayConfigSet(args[2], patch);
            return makeDisplayResult("display config saved",
                                     std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        if (args.size() == 2 && args[0] == "select") {
          if (!displaySelect) {
            return makeError("display select unavailable");
          }
          try {
            std::string structured = displaySelect(args[1]);
            return makeDisplayResult("display selected; restart required",
                                     std::move(structured));
          } catch (const std::exception &error) {
            return makeDisplayHookError(error);
          }
        }
        return makeError(
            "usage: display list|active|config get <key|active|default>|config "
            "set <key|default> <json-or-yaml-patch>|select <key>");
      });

  bus.registerHandler(
      "project", "project <args>",
      [projectCommand](std::vector<std::string> args) {
        if (!projectCommand) {
          return makeError("project command unavailable");
        }
        return projectCommand(args);
      });

  bus.registerHandler(
      "scene", "scene open <name-or-path> | scene save [args]",
      [sceneCommand](std::vector<std::string> args) {
        if (args.empty()) {
          return makeError("usage: scene open <name-or-path> | scene save [args]");
        }
        if (args[0] == "load") {
          return makeError("scene load was removed; use scene open");
        }
        if (!sceneCommand) {
          return makeError("scene command unavailable");
        }
        LX_core::CommandResult result = sceneCommand(args);
        if (args[0] == "open" && result.ok) {
          result.metadata[std::string(
              LX_core::kCommandResultClearUndoOnSuccessMetadataKey)] = "true";
          result.metadata[std::string(
              LX_core::kCommandResultClearRedoOnSuccessMetadataKey)] = "true";
        } else if (args[0] == "save" && result.ok) {
          result.metadata[std::string(
              LX_core::kCommandResultClearUndoOnSuccessMetadataKey)] = "false";
          result.metadata[std::string(
              LX_core::kCommandResultClearRedoOnSuccessMetadataKey)] = "false";
        }
        return result;
      });

  bus.registerHandler(
      "mode", "mode [selection|status]",
      [getEditMode, setEditMode](std::vector<std::string> args) {
        if (args.empty()) {
          const UiOverlay::EditorMode mode = modeFromCode(getEditMode());
          return makeOk("mode " + modeName(mode),
                        "{\"mode\":\"" + modeName(mode) + "\"}");
        }
        if (args.size() != 1) {
          return makeError("usage: mode [selection|status]");
        }
        if (args[0] == "status") {
          const UiOverlay::EditorMode mode = modeFromCode(getEditMode());
          return makeOk("mode " + modeName(mode),
                        "{\"mode\":\"" + modeName(mode) + "\"}");
        }
        if (args[0] == "orbit") {
          return makeError("mode orbit is no longer a camera control; use cam "
                           "control orbit");
        }
        if (args[0] == "freefly") {
          return makeError("mode freefly is no longer a camera control; use "
                           "cam control freefly");
        }
        const auto mode = parseMode(args[0]);
        if (!mode.has_value()) {
          return makeError("unknown mode: " + args[0]);
        }
        const UiOverlay::EditorMode previousMode = modeFromCode(getEditMode());
        setEditMode(modeCode(*mode));
        LX_core::CommandResult result =
            makeOk("mode " + modeName(*mode),
                   "{\"mode\":\"" + modeName(*mode) + "\"}");
        result.metadata["inverse.line"] = "mode " + modeName(previousMode);
        return result;
      });

  bus.registerHandler(
      "debug", "debug [on|off|status]",
      [debugEnabled, setDebugEnabled](std::vector<std::string> args) {
        if (args.empty() || args.size() > 1) {
          return makeError("usage: debug [on|off|status]");
        }
        if (!debugEnabled || !setDebugEnabled) {
          return makeError("debug state unavailable");
        }
        if (args[0] == "status") {
          return makeOk(debugEnabled() ? "debug on" : "debug off",
                        std::string("{\"debugEnabled\":") +
                            (debugEnabled() ? "true" : "false") + "}");
        }
        if (args[0] == "on") {
          setDebugEnabled(true);
          return makeOk("debug on", "{\"debugEnabled\":true}");
        }
        if (args[0] == "off") {
          setDebugEnabled(false);
          return makeOk("debug off", "{\"debugEnabled\":false}");
        }
        return makeError("usage: debug [on|off|status]");
      });

  bus.registerHandler(
      "recording",
      "recording status|enable|disable [force]|start "
      "[basic|diagnostic|trace]|stop [save|discard]",
      [recording, currentDocumentPath,
       buildInfoJson](std::vector<std::string> args) {
        if (!recording) {
          return makeError("recording unavailable");
        }
        const auto recorder = recording();
        if (!recorder.has_value()) {
          return makeError("recording unavailable");
        }
        RecordingController &controller = recorder->get();
        if (args.empty() || args[0] == "status") {
          const std::string structured =
              makeRecordingStatusJson(controller.status());
          return makeOk(structured, structured);
        }
        if (args[0] == "enable") {
          if (args.size() != 1) {
            return makeError("usage: recording enable");
          }
          controller.enable();
          return makeOk("recording enabled",
                        makeRecordingStatusJson(controller.status()));
        }
        if (args[0] == "disable") {
          if (args.size() > 2 || (args.size() == 2 && args[1] != "force")) {
            return makeError("usage: recording disable [force]");
          }
          if (!controller.disable(args.size() == 2)) {
            return makeError("recording active; use recording disable force");
          }
          return makeOk("recording disabled",
                        makeRecordingStatusJson(controller.status()));
        }
        if (args[0] == "start") {
          if (args.size() > 2) {
            return makeError("usage: recording start [basic|diagnostic|trace]");
          }
          const auto detailLevel = parseRecordingDetailLevel(args, 1);
          if (!detailLevel.has_value()) {
            return makeError("usage: recording start [basic|diagnostic|trace]");
          }
          const auto path =
              currentDocumentPath ? currentDocumentPath() : std::nullopt;
          const auto result = controller.start(RecordingStartOptions{
              .detailLevel = *detailLevel,
              .scenePath = path.value_or(std::string{}),
              .buildInfoJson =
                  buildInfoJson ? buildInfoJson() : std::string{"{}"},
          });
          return makeOk(
              "recording started",
              "{\"active\":" + std::string(result.active ? "true" : "false") +
                  ",\"sessionId\":\"" + jsonEscape(result.sessionId) + "\"}");
        }
        if (args[0] == "stop") {
          if (args.size() > 2 ||
              (args.size() == 2 && args[1] != "save" && args[1] != "discard")) {
            return makeError("usage: recording stop [save|discard]");
          }
          const bool save = args.size() < 2 || args[1] == "save";
          const auto result =
              controller.stop(RecordingStopOptions{.save = save});
          std::ostringstream oss;
          oss << "{\"saved\":" << (result.saved ? "true" : "false")
              << ",\"path\":\"" << jsonEscape(result.path.string()) << "\""
              << ",\"stepCount\":" << result.stepCount << ",\"sessionId\":\""
              << jsonEscape(result.sessionId) << "\"}";
          return makeOk(result.saved ? "recording saved" : "recording stopped",
                        oss.str());
        }
        return makeError("usage: recording status|enable|disable [force]|start "
                         "[basic|diagnostic|trace]|stop [save|discard]");
      });

  bus.registerHandler(
      "state", "state (summary|selection|cameras|scene|toolbar|history)",
      [editorState, scene, interaction, getEditMode, getCameraControlMode,
       dirty, permission, debugEnabled, currentDocumentPath, projectSummaryJson,
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
            .getCameraControlMode = getCameraControlMode,
            .setCameraControlMode = [](int) {},
            .sceneViewRect = [] { return SceneViewRect{}; },
            .dirty = dirty,
            .permission = permission,
            .debugEnabled = debugEnabled,
            .currentDocumentPath = currentDocumentPath,
            .projectSummaryJson = projectSummaryJson,
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
                              ? "\"" + jsonEscape(activeCamera->getPath()) +
                                    "\""
                              : "null") +
              ",\"editor\":" + makeCameraJson(editorState->getEditorCamera()) +
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
          oss << ",\"dirty\":" << (dirty() ? "true" : "false")
              << ",\"permission\":\"" << jsonEscape(permission()) << "\""
              << ",\"nodeCount\":" << scene->listAllPaths().size()
              << ",\"cameraCount\":" << scene->getCameras().size()
              << ",\"lightCount\":" << scene->getLights().size() << "}";
          const std::string structured = oss.str();
          return makeOk(structured, structured);
        }
        if (args[0] == "toolbar") {
          const std::string structured =
              "{\"mode\":\"" + modeName(modeFromCode(getEditMode())) +
              "\",\"camera\":\"" +
              cameraControlModeName(
                  cameraControlModeFromCode(getCameraControlMode())) +
              "\",\"previewEnabled\":" +
              std::string(editorState->isPreviewEnabled() ? "true" : "false") +
              ",\"debugEnabled\":" +
              std::string(debugEnabled && debugEnabled() ? "true" : "false") +
              "}";
          return makeOk(structured, structured);
        }
        if (args[0] == "history") {
          const std::vector<std::string> lines =
              persistedHistory ? persistedHistory()
                               : std::vector<std::string>{};
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
        if (editorState->isPreviewEnabled()) {
          return makeError("pick unavailable while preview is enabled");
        }
        try {
          LX_core::CommandResult result;
          if (args.size() == 2) {
            const float x = std::stof(args[0]);
            const float y = std::stof(args[1]);
            result = interaction->dispatchPickingClick(LX_core::Vec2f{x, y},
                                                       sceneViewRect());
          } else if (args.size() == 5 && args[0] == "screen") {
            const float x = std::stof(args[1]);
            const float y = std::stof(args[2]);
            const float viewportWidth = std::stof(args[3]);
            const float viewportHeight = std::stof(args[4]);
            result = interaction->dispatchPickingClick(
                LX_core::Vec2f{x, y},
                LX_core::Vec2f{viewportWidth, viewportHeight});
          } else {
            return makeError("usage: pick <x> <y> | pick screen <x> <y> "
                             "<viewport-width> <viewport-height>");
          }
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
      "mode", 0, [](const LX_core::CompletionContext &context) {
        static const std::vector<std::string> kModes = {"selection", "status"};
        std::vector<std::string> out;
        for (const auto &mode : kModes) {
          if (mode.rfind(context.partialToken, 0) == 0) {
            out.push_back(mode);
          }
        }
        return out;
      });
  bus.registerCompleter(
      "state", 0, [](const LX_core::CompletionContext &context) {
        static const std::vector<std::string> kTargets = {
            "summary", "selection", "cameras", "scene", "toolbar"};
        std::vector<std::string> out;
        for (const auto &target : kTargets) {
          if (target.rfind(context.partialToken, 0) == 0) {
            out.push_back(target);
          }
        }
        return out;
      });
  bus.registerCompleter("recording", 0,
                        [](const LX_core::CompletionContext &context) {
                          static const std::vector<std::string> kActions = {
                              "status", "enable", "disable", "start", "stop"};
                          std::vector<std::string> out;
                          for (const auto &action : kActions) {
                            if (action.rfind(context.partialToken, 0) == 0) {
                              out.push_back(action);
                            }
                          }
                          return out;
                        });
  bus.registerCompleter("display", 0,
                        [](const LX_core::CompletionContext &context) {
                          static const std::vector<std::string> kActions = {
                              "list", "active", "config", "select"};
                          std::vector<std::string> out;
                          for (const auto &action : kActions) {
                            if (action.rfind(context.partialToken, 0) == 0) {
                              out.push_back(action);
                            }
                          }
                          return out;
                        });
}

} // namespace LX_demo::lxe_editor
