#include "demos/lxe_editor/commands/register_lxe_editor_commands.hpp"

#include "core/editor/editor_state.hpp"
#include "core/scene/components/camera_component.hpp"
#include "core/scene/object.hpp"
#include "core/scene/scene.hpp"
#include "demos/lxe_editor/commands/lxe_editor_command_helpers.hpp"
#include "demos/lxe_editor/scene_interaction_controller.hpp"
#include "demos/lxe_editor/scene_view_rect.hpp"
#include "demos/lxe_editor/ui_overlay.hpp"

#include <cctype>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace LX_demo::lxe_editor {
namespace {

[[nodiscard]] std::string formatFloat(const float value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3) << value;
  return oss.str();
}

[[nodiscard]] std::string_view trimView(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                           text.front() == '\r' || text.front() == '\n')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                           text.back() == '\r' || text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

class ProjectSummaryJsonValidator final {
public:
  explicit ProjectSummaryJsonValidator(std::string_view text) : m_text(text) {}

  [[nodiscard]] bool isNullOrObject() {
    skipWhitespace();
    const bool valid = parseLiteral("null") || parseObject();
    skipWhitespace();
    return valid && atEnd();
  }

private:
  [[nodiscard]] bool atEnd() const { return m_index >= m_text.size(); }

  [[nodiscard]] char peek() const { return atEnd() ? '\0' : m_text[m_index]; }

  bool consume(const char expected) {
    if (peek() != expected) {
      return false;
    }
    ++m_index;
    return true;
  }

  void skipWhitespace() {
    while (!atEnd()) {
      const char c = peek();
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
        return;
      }
      ++m_index;
    }
  }

  bool parseLiteral(std::string_view literal) {
    if (m_text.substr(m_index, literal.size()) != literal) {
      return false;
    }
    m_index += literal.size();
    return true;
  }

  bool parseObject() {
    if (!consume('{')) {
      return false;
    }
    skipWhitespace();
    if (consume('}')) {
      return true;
    }

    while (true) {
      if (!parseString()) {
        return false;
      }
      skipWhitespace();
      if (!consume(':')) {
        return false;
      }
      skipWhitespace();
      if (!parseValue()) {
        return false;
      }
      skipWhitespace();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
      skipWhitespace();
    }
  }

  bool parseValue() {
    switch (peek()) {
    case '{':
      return parseObject();
    case '[':
      return parseArray();
    case '"':
      return parseString();
    case 't':
      return parseLiteral("true");
    case 'f':
      return parseLiteral("false");
    case 'n':
      return parseLiteral("null");
    default:
      return parseNumber();
    }
  }

  bool parseArray() {
    if (!consume('[')) {
      return false;
    }
    skipWhitespace();
    if (consume(']')) {
      return true;
    }

    while (true) {
      if (!parseValue()) {
        return false;
      }
      skipWhitespace();
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
      skipWhitespace();
    }
  }

  bool parseString() {
    if (!consume('"')) {
      return false;
    }
    while (!atEnd()) {
      const char c = m_text[m_index++];
      if (c == '"') {
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20U) {
        return false;
      }
      if (c != '\\') {
        continue;
      }
      if (atEnd()) {
        return false;
      }
      const char escaped = m_text[m_index++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
      case 'b':
      case 'f':
      case 'n':
      case 'r':
      case 't':
        break;
      case 'u':
        if (!parseHexQuad()) {
          return false;
        }
        break;
      default:
        return false;
      }
    }
    return false;
  }

  bool parseHexQuad() {
    if (m_index + 4 > m_text.size()) {
      return false;
    }
    for (usize i = 0; i < 4; ++i) {
      const auto c = static_cast<unsigned char>(m_text[m_index + i]);
      if (std::isxdigit(c) == 0) {
        return false;
      }
    }
    m_index += 4;
    return true;
  }

  bool parseNumber() {
    if (consume('-') && atEnd()) {
      return false;
    }

    if (consume('0')) {
      if (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        return false;
      }
    } else if (!parseDigits()) {
      return false;
    }

    if (consume('.')) {
      if (!parseDigits()) {
        return false;
      }
    }

    if (peek() == 'e' || peek() == 'E') {
      ++m_index;
      if (peek() == '+' || peek() == '-') {
        ++m_index;
      }
      if (!parseDigits()) {
        return false;
      }
    }

    return true;
  }

  bool parseDigits() {
    const usize start = m_index;
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
      ++m_index;
    }
    return m_index > start;
  }

  std::string_view m_text;
  usize m_index = 0;
};

[[nodiscard]] std::string sanitizeProjectSummaryJson(std::string text) {
  const std::string_view trimmed = trimView(text);
  if (!trimmed.empty() &&
      ProjectSummaryJsonValidator(trimmed).isNullOrObject()) {
    return std::string(trimmed);
  }
  return "null";
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
    return "{\"path\":\"" + editorCommandJsonEscape(node->getPath()) +
           "\",\"error\":\"missing_camera_component\"}";
  }

  std::ostringstream oss;
  oss << "{\"path\":\"" << editorCommandJsonEscape(node->getPath()) << "\""
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
    oss << '"' << editorCommandJsonEscape(selected[i]->getPath()) << '"';
  }
  oss << "]";
  if (const auto primary = editorState.getPrimarySelected();
      primary.has_value()) {
    oss << ",\"primaryPath\":\""
        << editorCommandJsonEscape(primary->get().getPath()) << '"';
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
  const std::string projectSummary = sanitizeProjectSummaryJson(
      context.projectSummaryJson ? context.projectSummaryJson()
                                 : std::string{});
  std::ostringstream oss;
  oss << "{\"sceneName\":\""
      << editorCommandJsonEscape(context.scene.getSceneName()) << "\""
      << ",\"dirty\":" << (context.dirty() ? "true" : "false")
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
    oss << '"' << editorCommandJsonEscape(activeCamera->getPath()) << '"';
  } else {
    oss << "null";
  }
  oss << ",\"documentPath\":";
  if (const auto path = context.runtimeScenePath(); path.has_value()) {
    oss << '"' << editorCommandJsonEscape(*path) << '"';
  } else {
    oss << "null";
  }
  oss << ",\"project\":" << (projectSummary.empty() ? "null" : projectSummary)
      << "}";
  return oss.str();
}

} // namespace

void registerRenderDebugCommands(LX_core::CommandBus &bus,
                                 const LxeEditorCommandContext &context) {
  std::reference_wrapper<LX_core::EditorState> editorState =
      context.editorState;
  std::reference_wrapper<LX_core::Scene> scene = context.scene;
  std::reference_wrapper<SceneInteractionController> interaction =
      context.interaction;
  auto getEditMode = context.getEditMode;
  auto setEditMode = context.setEditMode;
  auto getCameraControlMode = context.getCameraControlMode;
  auto sceneViewRect = context.sceneViewRect;
  auto dirty = context.dirty;
  auto debugEnabled = context.debugEnabled;
  auto setDebugEnabled = context.setDebugEnabled;
  auto runtimeScenePath = context.runtimeScenePath;
  auto projectSummaryJson = context.projectSummaryJson;
  auto persistedHistory = context.persistedHistory;
  interaction.get().setDebugLoggingHooks(context.debugEnabled,
                                         context.appendConsoleDebugLine);

  bus.registerHandler(
      "mode", "mode [selection|status]",
      [getEditMode, setEditMode](std::vector<std::string> args) {
        if (args.empty()) {
          const UiOverlay::EditorMode mode = modeFromCode(getEditMode());
          return makeEditorCommandOk("mode " + modeName(mode),
                                     "{\"mode\":\"" + modeName(mode) + "\"}");
        }
        if (args.size() != 1) {
          return makeEditorCommandError("usage: mode [selection|status]");
        }
        if (args[0] == "status") {
          const UiOverlay::EditorMode mode = modeFromCode(getEditMode());
          return makeEditorCommandOk("mode " + modeName(mode),
                                     "{\"mode\":\"" + modeName(mode) + "\"}");
        }
        if (args[0] == "orbit") {
          return makeEditorCommandError(
              "mode orbit is no longer a camera control; use cam "
              "control orbit");
        }
        if (args[0] == "freefly") {
          return makeEditorCommandError(
              "mode freefly is no longer a camera control; use "
              "cam control freefly");
        }
        const auto mode = parseMode(args[0]);
        if (!mode.has_value()) {
          return makeEditorCommandError("unknown mode: " + args[0]);
        }
        const UiOverlay::EditorMode previousMode = modeFromCode(getEditMode());
        setEditMode(modeCode(*mode));
        LX_core::CommandResult result =
            makeEditorCommandOk("mode " + modeName(*mode),
                                "{\"mode\":\"" + modeName(*mode) + "\"}");
        result.metadata["inverse.line"] = "mode " + modeName(previousMode);
        return result;
      });

  bus.registerHandler(
      "debug", "debug [on|off|status]",
      [debugEnabled, setDebugEnabled](std::vector<std::string> args) {
        if (args.empty() || args.size() > 1) {
          return makeEditorCommandError("usage: debug [on|off|status]");
        }
        if (!debugEnabled || !setDebugEnabled) {
          return makeEditorCommandError("debug state unavailable");
        }
        if (args[0] == "status") {
          return makeEditorCommandOk(debugEnabled() ? "debug on" : "debug off",
                                     std::string("{\"debugEnabled\":") +
                                         (debugEnabled() ? "true" : "false") +
                                         "}");
        }
        if (args[0] == "on") {
          setDebugEnabled(true);
          return makeEditorCommandOk("debug on", "{\"debugEnabled\":true}");
        }
        if (args[0] == "off") {
          setDebugEnabled(false);
          return makeEditorCommandOk("debug off", "{\"debugEnabled\":false}");
        }
        return makeEditorCommandError("usage: debug [on|off|status]");
      });

  bus.registerHandler(
      "state", "state (summary|selection|cameras|scene|toolbar|history)",
      [editorState, scene, interaction, getEditMode, getCameraControlMode,
       dirty, debugEnabled, runtimeScenePath, projectSummaryJson,
       persistedHistory](std::vector<std::string> args) {
        if (args.size() != 1) {
          return makeEditorCommandError(
              "usage: state (summary|selection|cameras|scene|toolbar|history)");
        }
        const LxeEditorCommandContext stateContext{
            .editorState = editorState.get(),
            .scene = scene.get(),
            .interaction = interaction.get(),
            .getEditMode = getEditMode,
            .setEditMode = [](int) {},
            .getCameraControlMode = getCameraControlMode,
            .setCameraControlMode = [](int) {},
            .sceneViewRect = [] { return SceneViewRect{}; },
            .dirty = dirty,
            .debugEnabled = debugEnabled,
            .runtimeScenePath = runtimeScenePath,
            .projectSummaryJson = projectSummaryJson,
            .persistedHistory = persistedHistory,
        };
        if (args[0] == "summary") {
          const std::string structured = makeSummaryJson(stateContext);
          return makeEditorCommandOk(structured, structured);
        }
        if (args[0] == "selection") {
          const std::string structured =
              makeSelectionJson(editorState.get(), interaction.get());
          return makeEditorCommandOk(structured, structured);
        }
        if (args[0] == "cameras") {
          const LX_core::SceneNodeSharedPtr activeCamera =
              editorState.get().resolveActiveCamera(scene.get());
          const std::string structured =
              "{\"activePath\":" +
              std::string(activeCamera ? "\"" +
                                             editorCommandJsonEscape(
                                                 activeCamera->getPath()) +
                                             "\""
                                       : "null") +
              ",\"editor\":" +
              makeCameraJson(editorState.get().getEditorCamera()) +
              ",\"game\":" +
              makeCameraJson(editorState.get().getPreviewCamera()) + "}";
          return makeEditorCommandOk(structured, structured);
        }
        if (args[0] == "scene") {
          std::ostringstream oss;
          oss << "{\"sceneName\":\""
              << editorCommandJsonEscape(scene.get().getSceneName())
              << "\",\"rootPath\":\""
              << editorCommandJsonEscape(
                     scene.get().getRootNode()
                         ? scene.get().getRootNode()->getPath()
                         : std::string("/"))
              << "\",\"documentPath\":";
          if (const auto path = runtimeScenePath(); path.has_value()) {
            oss << '"' << editorCommandJsonEscape(*path) << '"';
          } else {
            oss << "null";
          }
          oss << ",\"dirty\":" << (dirty() ? "true" : "false")
              << ",\"nodeCount\":" << scene.get().listAllPaths().size()
              << ",\"cameraCount\":" << scene.get().getCameras().size()
              << ",\"lightCount\":" << scene.get().getLights().size() << "}";
          const std::string structured = oss.str();
          return makeEditorCommandOk(structured, structured);
        }
        if (args[0] == "toolbar") {
          const std::string structured =
              "{\"mode\":\"" + modeName(modeFromCode(getEditMode())) +
              "\",\"camera\":\"" +
              cameraControlModeName(
                  cameraControlModeFromCode(getCameraControlMode())) +
              "\",\"previewEnabled\":" +
              std::string(editorState.get().isPreviewEnabled() ? "true"
                                                               : "false") +
              ",\"debugEnabled\":" +
              std::string(debugEnabled && debugEnabled() ? "true" : "false") +
              "}";
          return makeEditorCommandOk(structured, structured);
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
            oss << '"' << editorCommandJsonEscape(lines[i]) << '"';
          }
          oss << "],\"count\":" << lines.size() << "}";
          const std::string structured = oss.str();
          return makeEditorCommandOk(structured, structured);
        }
        return makeEditorCommandError("unknown state target: " + args[0]);
      });

  bus.registerHandler(
      "pick", "pick <x> <y>",
      [editorState, interaction, sceneViewRect](std::vector<std::string> args) {
        if (editorState.get().isPreviewEnabled()) {
          return makeEditorCommandError(
              "pick unavailable while preview is enabled");
        }
        try {
          LX_core::CommandResult result;
          if (args.size() == 2) {
            const float x = std::stof(args[0]);
            const float y = std::stof(args[1]);
            result = interaction.get().dispatchPickingClick(
                LX_core::Vec2f{x, y}, sceneViewRect());
          } else if (args.size() == 5 && args[0] == "screen") {
            const float x = std::stof(args[1]);
            const float y = std::stof(args[2]);
            const float viewportWidth = std::stof(args[3]);
            const float viewportHeight = std::stof(args[4]);
            result = interaction.get().dispatchPickingClick(
                LX_core::Vec2f{x, y},
                LX_core::Vec2f{viewportWidth, viewportHeight});
          } else {
            return makeEditorCommandError(
                "usage: pick <x> <y> | pick screen <x> <y> "
                "<viewport-width> <viewport-height>");
          }
          if (!result.ok) {
            return result;
          }
          const std::string structured =
              makeSelectionJson(editorState.get(), interaction.get());
          return makeEditorCommandOk(result.message, structured);
        } catch (...) {
          return makeEditorCommandError("invalid float for pick");
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
}

} // namespace LX_demo::lxe_editor
