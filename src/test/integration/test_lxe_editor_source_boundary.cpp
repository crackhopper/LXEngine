#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

std::string readText(const std::filesystem::path &path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

bool fileContains(const std::filesystem::path &path,
                  const std::string &needle) {
  if (!std::filesystem::is_regular_file(path)) {
    return false;
  }
  return readText(path).find(needle) != std::string::npos;
}

struct LegacyTokenAudit final {
  std::string token;
  std::vector<std::string> allowedPathSubstrings;
};

struct LegacyTokenLineMarkerAllowance final {
  std::string pathSubstring;
  std::string marker;
};

bool pathAllowed(const std::string &relativePath,
                 const std::vector<std::string> &allowedPathSubstrings) {
  return std::any_of(allowedPathSubstrings.begin(),
                     allowedPathSubstrings.end(),
                     [&](const std::string &needle) {
                       return relativePath.find(needle) != std::string::npos;
                     });
}

bool lineMarkerAllowed(
    const std::string &relativePath, const std::string &line,
    const std::vector<LegacyTokenLineMarkerAllowance> &allowances) {
  return std::any_of(allowances.begin(), allowances.end(),
                     [&](const LegacyTokenLineMarkerAllowance &allowance) {
                       return relativePath.find(allowance.pathSubstring) !=
                                  std::string::npos &&
                              line.find(allowance.marker) != std::string::npos;
                     });
}

std::vector<std::filesystem::path>
filesContaining(const std::filesystem::path &root,
                const std::vector<std::string> &needles) {
  std::vector<std::filesystem::path> hits;
  if (!std::filesystem::exists(root)) {
    return hits;
  }
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    if (path.filename() == "test_lxe_editor_source_boundary.cpp") {
      continue;
    }
    const std::string ext = path.extension().string();
    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".txt" &&
        path.filename() != "CMakeLists.txt") {
      continue;
    }
    const std::string text = readText(path);
    for (const std::string &needle : needles) {
      if (text.find(needle) != std::string::npos) {
        hits.push_back(path);
        break;
      }
    }
  }
  return hits;
}

void expectNoHits(const std::vector<std::filesystem::path> &hits,
                  const std::string &label) {
  if (hits.empty()) {
    return;
  }
  std::cerr << "[FAIL] " << label << '\n';
  for (const auto &hit : hits) {
    std::cerr << "  " << hit.generic_string() << '\n';
  }
  ++g_failures;
}

bool isAuditedTextFile(const std::filesystem::path &path) {
  const std::string ext = path.extension().string();
  return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".frag" ||
         ext == ".vert" || ext == ".comp" || ext == ".glsl" ||
         ext == ".yaml" || ext == ".yml" || ext == ".md" || ext == ".txt" ||
         path.filename() == "CMakeLists.txt";
}

void expectNoDisallowedLegacyTokens(
    const std::filesystem::path &sourceRoot,
    const std::vector<LegacyTokenAudit> &audits,
    const std::vector<LegacyTokenLineMarkerAllowance> &lineMarkerAllowances) {
  const std::vector<std::filesystem::path> roots{
      sourceRoot / "src", sourceRoot / "assets", sourceRoot / "docs",
      sourceRoot / "notes"};

  for (const auto &audit : audits) {
    std::vector<std::string> violations;
    for (const auto &root : roots) {
      if (!std::filesystem::exists(root)) {
        continue;
      }
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !isAuditedTextFile(entry.path())) {
          continue;
        }

        std::ifstream in(entry.path());
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(in, line)) {
          ++lineNumber;
          if (line.find(audit.token) == std::string::npos) {
            continue;
          }
          const auto relative = entry.path().lexically_relative(sourceRoot);
          const std::string relativePath = relative.generic_string();
          const std::string diagnosticLine =
              relativePath + ":" + std::to_string(lineNumber) + ": " + line;
          if (!pathAllowed(relativePath, audit.allowedPathSubstrings) &&
              !lineMarkerAllowed(relativePath, line, lineMarkerAllowances)) {
            violations.push_back(diagnosticLine);
          }
        }
      }
    }

    if (!violations.empty()) {
      std::sort(violations.begin(), violations.end());
      std::cerr << "[FAIL] legacy IBL token '" << audit.token
                << "' is only allowed in named negative audits/docs\n";
      for (const auto &violation : violations) {
        std::cerr << "  " << violation << '\n';
      }
      ++g_failures;
    }
  }
}

} // namespace

int main() {
#ifndef LXE_SOURCE_DIR
#error "LXE_SOURCE_DIR must be defined for test_lxe_editor_source_boundary"
#endif

  const std::string legacyCore = std::string("core") + "/" + "editor";
  const std::string legacyDemo = std::string("demos") + "/" + "lxe_editor";
  const std::string legacySrc = "src/";
  const std::string legacyCoreInc = std::string("#include \"") + legacyCore + "/";
  const std::string legacyCoreInc2 = std::string("#include <") + legacyCore + "/";
  const std::string legacyDemoInc = std::string("#include \"") + legacyDemo + "/";
  const std::string legacyDemoInc2 = std::string("#include <") + legacyDemo + "/";
  const std::string legacySrcCore = legacySrc + legacyCore;
  const std::string legacySrcDemo = legacySrc + legacyDemo;

  const std::filesystem::path sourceRoot = LXE_SOURCE_DIR;
  const auto coreEditorDir = sourceRoot / "src" / "core" / "editor";
  const auto demoEditorDir = sourceRoot / "src" / "demos" / "lxe_editor";
  const auto editorDir = sourceRoot / "src" / "editor";
  const auto rootCMake = sourceRoot / "CMakeLists.txt";
  const auto demosCMake = sourceRoot / "src" / "demos" / "CMakeLists.txt";
  const std::string docsSuperpowersPath = "docs/superpowers";
  const std::string notesSuperpowersPath = "notes/superpowers";

  EXPECT(std::filesystem::is_directory(editorDir),
         "src/editor must exist after editor promotion");
  EXPECT(!std::filesystem::exists(coreEditorDir),
         "legacy production editor path under src/core must not remain");
  EXPECT(!std::filesystem::exists(demoEditorDir),
         "legacy production demo editor path under src/demos must not remain");
  EXPECT(fileContains(rootCMake, "add_subdirectory(src/editor)"),
         "root CMake must add src/editor");
  EXPECT(!fileContains(demosCMake, "lxe_editor"),
         "src/demos/CMakeLists.txt must not add lxe_editor");

  expectNoHits(filesContaining(sourceRoot / "src",
                               {legacyCoreInc,
                                legacyCoreInc2,
                                legacyDemoInc,
                                legacyDemoInc2}),
               "production source must not include old editor paths");
  const std::string srcCorePrefix = std::string("src") + "/" + "core";
  expectNoHits(filesContaining(sourceRoot / srcCorePrefix,
                               {"CommandBus", "EditorState",
                                "ConsolePanel", "InspectorPanel",
                                "SceneTreePanel", "ViewportOverlay",
                                "GizmoAdapter"}),
               "src/core must not mention editor application classes");

  const std::vector<LegacyTokenAudit> audits = {
      {"bakeStaticEnvironment", // named-negative-legacy-token-audit
       {"notes/requirements", docsSuperpowersPath, notesSuperpowersPath}},
      {"IblBakeRenderer", // named-negative-legacy-token-audit
       {"notes/requirements", docsSuperpowersPath, notesSuperpowersPath}},
      {"HAS_IBL", // named-negative-legacy-token-audit
       {"notes/requirements", docsSuperpowersPath, notesSuperpowersPath}},
      {"iblIntensity", // named-negative-legacy-token-audit
       {"src/core/scene/ibl_environment.hpp", "notes/requirements",
        docsSuperpowersPath, notesSuperpowersPath}},
      {"ForwardIblLighting", // named-negative-legacy-token-audit
       {"src/test/integration/test_render_resource_parsers.cpp",
        "notes/requirements", docsSuperpowersPath, notesSuperpowersPath}},
      {"feature.iblLighting", // named-negative-legacy-token-audit
       {docsSuperpowersPath, notesSuperpowersPath}},
  };
  const std::vector<LegacyTokenLineMarkerAllowance> lineMarkerAllowances = {
      {"src/test/integration/test_lxe_editor_source_boundary.cpp",
       "named-negative-legacy-token-audit"},
      {"src/test/integration/test_shader_compiler.cpp",
       "named-negative-ibl-formula-audit"},
  };
  expectNoDisallowedLegacyTokens(sourceRoot, audits, lineMarkerAllowances);

  if (g_failures != 0) {
    std::cerr << g_failures << " editor source boundary checks failed\n";
    return 1;
  }
  return 0;
}
