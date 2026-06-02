#include "demos/lxe_editor/scene_document.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace demo = LX_demo::lxe_editor;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

[[nodiscard]] std::filesystem::path makeTempPath(const char *filename) {
  return std::filesystem::temp_directory_path() / filename;
}

[[nodiscard]] const demo::SceneNodeDocument *
findChildByName(const demo::SceneNodeDocument &parent,
                std::string_view nodeName) {
  for (const auto &child : parent.children) {
    if (child.nodeName == nodeName) {
      return &child;
    }
  }
  return nullptr;
}

[[nodiscard]] const demo::SceneNodeDocument *
findChildByNodeName(const demo::SceneNodeDocument &parent,
                    std::string_view nodeName) {
  for (const auto &child : parent.children) {
    if (child.nodeName == nodeName) {
      return &child;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string readFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

void testLoadExplicitRootSceneDocumentReadsGameAndEditorCamera() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_load.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "  gameplayCameraPath: /world/game_cam\n"
         "root:\n"
         "  nodeName: scene_root\n"
         "  name: ''\n"
         "  transform:\n"
         "    translation: [0.0, 0.0, 0.0]\n"
         "    rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "    scale: [1.0, 1.0, 1.0]\n"
         "  visibilityMask: 4294967295\n"
         "  children:\n"
         "    - nodeName: world_root\n"
         "      name: world\n"
         "      transform:\n"
         "        translation: [0.0, 0.0, 0.0]\n"
         "        rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "        scale: [1.0, 1.0, 1.0]\n"
         "      visibilityMask: 4294967295\n"
         "      children:\n"
         "        - nodeName: game_camera\n"
         "          name: game_cam\n"
         "          transform:\n"
         "            translation: [0.0, 2.0, 6.0]\n"
         "            rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "            scale: [1.0, 1.0, 1.0]\n"
         "          visibilityMask: 4294967295\n"
         "          camera:\n"
         "            eye: [0.0, 2.0, 6.0]\n"
         "            target: [0.0, 0.0, 0.0]\n"
         "            up: [0.0, 1.0, 0.0]\n"
         "            type: perspective\n"
         "            fovY: 45.0\n"
         "            aspect: 1.7777778\n"
         "            nearPlane: 0.1\n"
         "            farPlane: 1000.0\n"
         "            left: -1.0\n"
         "            right: 1.0\n"
         "            bottom: -1.0\n"
         "            top: 1.0\n"
         "            cullingMask: 4294967295\n"
         "        - nodeName: ground\n"
         "          name: ground\n"
         "          transform:\n"
         "            translation: [0.0, -1.5, 0.0]\n"
         "            rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "            scale: [1.0, 1.0, 1.0]\n"
         "          visibilityMask: 4294967295\n"
         "          mesh:\n"
         "            uri: builtin://lxe_editor/ground_mesh\n"
         "          material:\n"
         "            uri: builtin://lxe_editor/ground_material\n"
         "        - nodeName: dir_light_node\n"
         "          name: dir_light\n"
         "          transform:\n"
         "            translation: [0.0, 0.0, 0.0]\n"
         "            rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "            scale: [1.0, 1.0, 1.0]\n"
         "          visibilityMask: 4294967295\n"
         "          directionalLight:\n"
         "            direction: [-0.3, -1.0, -0.5]\n"
         "            color: [1.0, 0.98, 0.9]\n"
         "            intensity: 1.0\n"
         "editor:\n"
         "  editorCamera:\n"
         "    position: [5.0, 6.0, 7.0]\n"
         "    rotationEulerDeg: [0.0, 90.0, 0.0]\n"
         "    fovY: 35.0\n"
         "    nearPlane: 0.2\n"
         "    farPlane: 400.0\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.sceneName() == "lxe_editor", "scene name should load");
  EXPECT(doc.gameplayCameraPath() == "/world/game_cam",
         "gameplay camera path should load");
  EXPECT(doc.rootNode().nodeName == "scene_root",
         "explicit root node should load");
  EXPECT(doc.rootNode().children.size() == 1,
         "explicit root should own top-level nodes");
  const demo::SceneNodeDocument *world =
      findChildByName(doc.rootNode(), "world_root");
  EXPECT(world != nullptr, "world should exist under root");
  if (world == nullptr) {
    return;
  }
  EXPECT(world->children.size() == 3, "world children should load");
  const demo::SceneNodeDocument *gameCamera =
      findChildByName(*world, "game_camera");
  EXPECT(gameCamera != nullptr, "camera child should load");
  if (gameCamera == nullptr) {
    return;
  }
  EXPECT(gameCamera->camera.has_value(), "camera node should load");
  EXPECT(gameCamera->transform.translation.y == 2.0f,
         "legacy camera eye should migrate into node transform");
  EXPECT(gameCamera->camera->focusDistance > 6.0f,
         "legacy camera target should migrate into focus distance");
  const demo::SceneNodeDocument *ground = findChildByName(*world, "ground");
  EXPECT(ground != nullptr, "ground child should load");
  if (ground == nullptr) {
    return;
  }
  EXPECT(ground->meshUri.has_value(), "mesh uri should load");
  EXPECT(*ground->meshUri == "builtin://lxe_editor/ground_mesh",
         "mesh uri should survive load");
  const demo::SceneNodeDocument *light =
      findChildByName(*world, "dir_light_node");
  EXPECT(light != nullptr, "directional light child should load");
  if (light == nullptr) {
    return;
  }
  EXPECT(light->light.has_value(), "directional light should load");
  EXPECT(light->light->kind == demo::LightKind::Directional,
         "legacy directional light should migrate to typed kind");
  EXPECT(light->light->color.y == 0.98f, "directional light color should load");
  EXPECT(doc.hasEditorCamera(), "editor camera metadata should load");
  EXPECT(doc.editorCamera().position.x == 5.0f, "editor camera x should load");
  EXPECT(doc.editorCamera().rotationEulerDeg.y == 90.0f,
         "editor camera yaw should load");
  EXPECT(doc.editorCamera().fovY == 35.0f, "editor camera fov should load");
  EXPECT(doc.editorCamera().nearPlane == 0.2f,
         "editor camera near plane should load");
  EXPECT(doc.editorCamera().farPlane == 400.0f,
         "editor camera far plane should load");
}

void testLoadLegacySceneDocumentNormalizesUnderExplicitRoot() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_legacy.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "  gameplayCameraPath: /world/game_cam\n"
         "nodes:\n"
         "  - nodeName: world_root\n"
         "    name: world\n"
         "    transform:\n"
         "      translation: [0.0, 0.0, 0.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "  - nodeName: game_camera\n"
         "    name: game_cam\n"
         "    parentPath: /world\n"
         "    transform:\n"
         "      translation: [0.0, 2.0, 6.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "    camera:\n"
         "      eye: [0.0, 2.0, 6.0]\n"
         "      target: [0.0, 0.0, 0.0]\n"
         "      up: [0.0, 1.0, 0.0]\n"
         "      type: perspective\n"
         "      fovY: 45.0\n"
         "      aspect: 1.7777778\n"
         "      nearPlane: 0.1\n"
         "      farPlane: 1000.0\n"
         "      left: -1.0\n"
         "      right: 1.0\n"
         "      bottom: -1.0\n"
         "      top: 1.0\n"
         "      cullingMask: 4294967295\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.rootNode().children.size() == 1,
         "legacy load should normalize top-level nodes under explicit root");
  const demo::SceneNodeDocument *world =
      findChildByName(doc.rootNode(), "world_root");
  EXPECT(world != nullptr, "legacy world should normalize under root");
  if (world == nullptr) {
    return;
  }
  EXPECT(world->children.size() == 1,
         "legacy child links should normalize recursively");
  const demo::SceneNodeDocument *gameCamera =
      findChildByName(*world, "game_camera");
  EXPECT(gameCamera != nullptr, "legacy camera should normalize under world");
}

void testLoadLegacySceneDocumentNormalizesNodeNameBasedParentPaths() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_legacy_node_name.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "  gameplayCameraPath: /node_world/game_camera\n"
         "nodes:\n"
         "  - nodeName: node_world\n"
         "    name: world\n"
         "    transform:\n"
         "      translation: [0.0, 0.0, 0.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "  - nodeName: game_camera\n"
         "    name: game_cam\n"
         "    parentPath: /node_world\n"
         "    transform:\n"
         "      translation: [0.0, 2.0, 6.0]\n"
         "      rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "      scale: [1.0, 1.0, 1.0]\n"
         "    visibilityMask: 4294967295\n"
         "    camera:\n"
         "      eye: [0.0, 2.0, 6.0]\n"
         "      target: [0.0, 0.0, 0.0]\n"
         "      up: [0.0, 1.0, 0.0]\n"
         "      type: perspective\n"
         "      fovY: 45.0\n"
         "      aspect: 1.7777778\n"
         "      nearPlane: 0.1\n"
         "      farPlane: 1000.0\n"
         "      left: -1.0\n"
         "      right: 1.0\n"
         "      bottom: -1.0\n"
         "      top: 1.0\n"
         "      cullingMask: 4294967295\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  const demo::SceneNodeDocument *world =
      findChildByNodeName(doc.rootNode(), "node_world");
  EXPECT(world != nullptr,
         "legacy nodeName path should resolve top-level parent");
  if (world == nullptr) {
    return;
  }
  const demo::SceneNodeDocument *gameCamera =
      findChildByNodeName(*world, "game_camera");
  EXPECT(
      gameCamera != nullptr,
      "legacy nodeName-based parent path should normalize under explicit root");
}

void testLoadMalformedExplicitRootDocumentFailsClearly() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_bad_root.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "  gameplayCameraPath: /game_cam\n"
         "root: []\n";
  out.close();

  bool threw = false;
  try {
    (void)demo::loadSceneDocument(path);
  } catch (const std::runtime_error &error) {
    threw =
        std::string_view(error.what()).find("root") != std::string_view::npos;
  }

  EXPECT(
      threw,
      "malformed explicit-root documents should fail with root-specific error");
}

void testLoadExplicitRootDocumentRejectsUnsupportedRootPayload() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_root_payload.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "  gameplayCameraPath: /game_cam\n"
         "root:\n"
         "  nodeName: scene_root\n"
         "  name: ''\n"
         "  mesh:\n"
         "    uri: builtin://lxe_editor/ground_mesh\n";
  out.close();

  bool threw = false;
  try {
    (void)demo::loadSceneDocument(path);
  } catch (const std::runtime_error &error) {
    const std::string_view message(error.what());
    threw = message.find("root") != std::string_view::npos &&
            message.find("payload") != std::string_view::npos;
  }

  EXPECT(
      threw,
      "explicit-root documents should reject unsupported root payload fields");
}

void testLoadExplicitRootDocumentRejectsNonCanonicalRootIdentity() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_root_identity.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: lxe_editor\n"
         "  gameplayCameraPath: /game_cam\n"
         "root:\n"
         "  nodeName: custom_root\n"
         "  name: root\n";
  out.close();

  bool threw = false;
  try {
    (void)demo::loadSceneDocument(path);
  } catch (const std::runtime_error &error) {
    const std::string_view message(error.what());
    threw = message.find("root") != std::string_view::npos &&
            message.find("identity") != std::string_view::npos;
  }

  EXPECT(threw,
         "explicit-root documents should reject non-canonical root identity");
}

void testSaveSceneDocumentWritesExplicitRootCanonicalFormat() {
  demo::SceneDocument doc;
  doc.setSceneName("lxe_editor");
  doc.setGameplayCameraPath("/world/game_cam");
  doc.setEnvironment(demo::EnvironmentState{
      .enabled = true,
      .hdrUri = "assets/env/studio_small_03_2k.hdr",
      .skyboxEnabled = true,
      .intensity = 1.25f,
      .roughnessMipCount = 6.0f,
  });
  auto &root = doc.mutableRootNode();
  root.nodeName = "scene_root";

  demo::SceneNodeDocument world{
      .nodeName = "world_root",
      .name = "world",
  };
  world.children.push_back(demo::SceneNodeDocument{
      .nodeName = "game_camera",
      .name = "game_cam",
      .transform =
          {
              .translation = {1.0f, 2.0f, 3.0f},
              .rotation = LX_core::Quatf{1.0f, 0.0f, 0.0f, 0.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          },
      .camera =
          demo::CameraNodeState{
              .fovY = 55.0f,
              .aspect = 1.5f,
              .nearPlane = 0.5f,
              .farPlane = 250.0f,
              .focusDistance = 3.75f,
          },
  });
  world.children.push_back(demo::SceneNodeDocument{
      .nodeName = "helmet",
      .name = "helmet",
      .meshUri = std::string("assets/models/damaged_helmet/DamagedHelmet.gltf"),
      .materialUri =
          std::string("assets/materials/blinnphong_textured.material"),
      .proceduralMaterial =
          demo::ProceduralMaterialState{
              .enabled = true,
              .binding = "ShadertoyUBO",
              .timeMember = "time",
              .resolutionMember = "resolution",
              .audioBandsMember = "audioBands",
          },
      .nodeMaterialOverrides =
          demo::MaterialOverrideState{
              .baseColor = LX_core::Vec3f{0.8f, 0.2f, 0.2f},
              .parameters =
                  {{"MaterialUBO.mixAmount",
                    LX_core::MaterialParameterValue{
                        .type = LX_core::MaterialParameterValueType::Float,
                        .floatValue = 0.35f}}},
          },
      .materialOverrides =
          demo::MaterialOverrideState{
              .baseColor = LX_core::Vec3f{0.4f, 0.5f, 0.6f},
              .parameters = {{"MaterialUBO.mode",
                              LX_core::MaterialParameterValue{
                                  .type =
                                      LX_core::MaterialParameterValueType::Int,
                                  .intValue = 1}}},
          },
  });
  world.children.push_back(demo::SceneNodeDocument{
      .nodeName = "dir_light_node",
      .name = "dir_light",
      .light =
          demo::LightNodeState{
              .kind = demo::LightKind::Directional,
              .direction = {-0.3f, -1.0f, -0.5f},
              .color = {1.0f, 0.98f, 0.9f},
              .intensity = 2.0f,
          },
  });
  root.children.push_back(std::move(world));
  doc.setEditorCamera(demo::EditorCameraState{
      .position = {7.0f, 8.0f, 9.0f},
      .rotationEulerDeg = {10.0f, 20.0f, 30.0f},
      .fovY = 35.0f,
      .nearPlane = 0.2f,
      .farPlane = 400.0f,
  });

  const std::filesystem::path path =
      makeTempPath("lx_scene_document_roundtrip.yaml");
  demo::saveSceneDocument(path, doc);

  const std::string savedText = readFile(path);
  EXPECT(savedText.find("\nroot:\n") != std::string::npos,
         "canonical save should write explicit root");
  EXPECT(savedText.find("\nnodes:\n") == std::string::npos,
         "canonical save should not write legacy flat nodes");
  EXPECT(savedText.find("\n            eye:") == std::string::npos &&
             savedText.find("\n            target:") == std::string::npos &&
             savedText.find("\n            up:") == std::string::npos,
         "canonical save should keep camera pose on node transform");
  EXPECT(savedText.find("\n            left:") == std::string::npos &&
             savedText.find("\n            right:") == std::string::npos &&
             savedText.find("\n            bottom:") == std::string::npos &&
             savedText.find("\n            top:") == std::string::npos,
         "canonical perspective camera should not write frustum bounds");

  const demo::SceneDocument loaded = demo::loadSceneDocument(path);
  EXPECT(loaded.sceneName() == "lxe_editor",
         "scene name should survive round trip");
  EXPECT(loaded.gameplayCameraPath() == "/world/game_cam",
         "gameplay camera path should survive round trip");
  EXPECT(loaded.hasEnvironment(),
         "scene environment should survive round trip");
  if (loaded.hasEnvironment()) {
    EXPECT(loaded.environment().enabled,
           "scene environment enabled flag should round trip");
    EXPECT(loaded.environment().hdrUri ==
               "assets/env/studio_small_03_2k.hdr",
           "scene environment HDR URI should round trip");
    EXPECT(loaded.environment().skyboxEnabled,
           "scene environment skybox flag should round trip");
    EXPECT(loaded.environment().intensity == 1.25f,
           "scene environment intensity should round trip");
    EXPECT(loaded.environment().roughnessMipCount == 6.0f,
           "scene environment roughness mip count should round trip");
  }
  EXPECT(loaded.rootNode().children.size() == 1,
         "explicit root should survive round trip");
  const demo::SceneNodeDocument *loadedWorld =
      findChildByName(loaded.rootNode(), "world_root");
  EXPECT(loadedWorld != nullptr, "world should survive round trip");
  if (loadedWorld == nullptr) {
    return;
  }
  EXPECT(loadedWorld->children.size() == 3,
         "world children should survive round trip");
  const demo::SceneNodeDocument *loadedCamera =
      findChildByName(*loadedWorld, "game_camera");
  EXPECT(loadedCamera != nullptr, "camera node should survive round trip");
  if (loadedCamera == nullptr) {
    return;
  }
  EXPECT(loadedCamera->camera.has_value(),
         "camera payload should survive round trip");
  EXPECT(loadedCamera->transform.translation.x == 1.0f,
         "camera node transform should survive round trip");
  EXPECT(loadedCamera->camera->nearPlane == 0.5f,
         "camera near plane should survive round trip");
  EXPECT(loadedCamera->camera->focusDistance == 3.75f,
         "camera focus distance should survive round trip");
  const demo::SceneNodeDocument *loadedHelmet =
      findChildByName(*loadedWorld, "helmet");
  EXPECT(loadedHelmet != nullptr, "helmet should survive round trip");
  if (loadedHelmet == nullptr) {
    return;
  }
  EXPECT(loadedHelmet->meshUri.has_value(),
         "mesh uri should survive round trip");
  EXPECT(*loadedHelmet->meshUri ==
             "assets/models/damaged_helmet/DamagedHelmet.gltf",
         "mesh uri should round trip");
  EXPECT(loadedHelmet->materialUri.has_value(),
         "material uri should survive round trip");
  EXPECT(loadedHelmet->proceduralMaterial.enabled,
         "procedural material opt-in should survive round trip");
  EXPECT(loadedHelmet->proceduralMaterial.binding == "ShadertoyUBO",
         "procedural material binding should round trip");
  EXPECT(loadedHelmet->proceduralMaterial.timeMember == "time",
         "procedural material time member should round trip");
  EXPECT(loadedHelmet->proceduralMaterial.resolutionMember == "resolution",
         "procedural material resolution member should round trip");
  EXPECT(loadedHelmet->proceduralMaterial.audioBandsMember.has_value() &&
             *loadedHelmet->proceduralMaterial.audioBandsMember == "audioBands",
         "procedural material audio member should round trip");
  EXPECT(loadedHelmet->nodeMaterialOverrides.baseColor.has_value(),
         "node baseColor override should survive round trip");
  EXPECT(loadedHelmet->nodeMaterialOverrides.baseColor->x == 0.8f &&
             loadedHelmet->nodeMaterialOverrides.baseColor->y == 0.2f &&
             loadedHelmet->nodeMaterialOverrides.baseColor->z == 0.2f,
         "node baseColor override value should round trip");
  EXPECT(loadedHelmet->materialOverrides.baseColor.has_value(),
         "material-side baseColor override should survive round trip");
  EXPECT(loadedHelmet->materialOverrides.baseColor->x == 0.4f &&
             loadedHelmet->materialOverrides.baseColor->y == 0.5f &&
             loadedHelmet->materialOverrides.baseColor->z == 0.6f,
         "material-side baseColor override value should round trip");
  EXPECT(loadedHelmet->nodeMaterialOverrides.parameters.count(
             "MaterialUBO.mixAmount") == 1,
         "generic node material parameter should survive round trip");
  EXPECT(
      loadedHelmet->nodeMaterialOverrides.parameters.at("MaterialUBO.mixAmount")
              .type == LX_core::MaterialParameterValueType::Float,
      "generic node material float type should survive round trip");
  EXPECT(loadedHelmet->materialOverrides.parameters.count("MaterialUBO.mode") ==
             1,
         "generic material-side int parameter should survive round trip");
  const demo::SceneNodeDocument *loadedLight =
      findChildByName(*loadedWorld, "dir_light_node");
  EXPECT(loadedLight != nullptr, "light should survive round trip");
  if (loadedLight == nullptr) {
    return;
  }
  EXPECT(loadedLight->light.has_value(),
         "directional light should survive round trip");
  EXPECT(loadedLight->light->kind == demo::LightKind::Directional,
         "directional light kind should survive round trip");
  EXPECT(loadedLight->light->intensity == 2.0f,
         "directional light intensity should survive round trip");
  EXPECT(loaded.hasEditorCamera(),
         "editor camera metadata should survive round trip");
  EXPECT(loaded.editorCamera().position.z == 9.0f,
         "editor camera position should survive round trip");
  EXPECT(loaded.editorCamera().rotationEulerDeg.x == 10.0f,
         "editor camera rotation should survive round trip");
  EXPECT(loaded.editorCamera().fovY == 35.0f,
         "editor camera fov should survive round trip");
  EXPECT(loaded.editorCamera().nearPlane == 0.2f,
         "editor camera near plane should survive round trip");
  EXPECT(loaded.editorCamera().farPlane == 400.0f,
         "editor camera far plane should survive round trip");
}

void testSaveSceneDocumentRejectsUnsupportedRootPayload() {
  demo::SceneDocument doc;
  doc.setSceneName("lxe_editor");
  auto &root = doc.mutableRootNode();
  root.nodeName = "scene_root";
  root.meshUri = std::string("builtin://lxe_editor/ground_mesh");

  const std::filesystem::path path =
      makeTempPath("lx_scene_document_bad_save_payload.yaml");

  bool threw = false;
  try {
    demo::saveSceneDocument(path, doc);
  } catch (const std::runtime_error &error) {
    const std::string_view message(error.what());
    threw = message.find("root") != std::string_view::npos &&
            message.find("payload") != std::string_view::npos;
  }

  EXPECT(threw,
         "save should reject unsupported payload fields on the explicit root");
}

void testSceneDocumentRoundTripsTypedLightPayloads() {
  demo::SceneDocument doc;
  auto &root = doc.mutableRootNode();
  root.children.push_back(demo::SceneNodeDocument{
      .nodeName = "sun_node",
      .name = "sun",
      .light =
          demo::LightNodeState{
              .kind = demo::LightKind::Directional,
              .direction = {-0.3f, -1.0f, -0.5f},
              .color = {1.0f, 0.98f, 0.9f},
              .intensity = 1.0f,
          },
  });
  root.children.push_back(demo::SceneNodeDocument{
      .nodeName = "point_node",
      .name = "point",
      .light =
          demo::LightNodeState{
              .kind = demo::LightKind::Point,
              .color = {0.8f, 0.7f, 0.6f},
              .intensity = 2.0f,
              .range = 6.0f,
          },
  });
  root.children.push_back(demo::SceneNodeDocument{
      .nodeName = "spot_node",
      .name = "spot",
      .light =
          demo::LightNodeState{
              .kind = demo::LightKind::Spot,
              .direction = {0.0f, -0.5f, -1.0f},
              .color = {0.7f, 0.8f, 1.0f},
              .intensity = 3.0f,
              .range = 8.0f,
              .innerConeDegrees = 20.0f,
              .outerConeDegrees = 35.0f,
          },
  });

  const std::filesystem::path path =
      makeTempPath("lx_scene_document_typed_lights.yaml");
  demo::saveSceneDocument(path, doc);
  const std::string savedText = readFile(path);
  EXPECT(savedText.find("light:") != std::string::npos,
         "save should write typed light payloads");
  EXPECT(savedText.find("directionalLight:") == std::string::npos,
         "save should not write legacy directionalLight payloads");

  const demo::SceneDocument loaded = demo::loadSceneDocument(path);
  EXPECT(loaded.rootNode().children.size() == 3,
         "all typed lights should round trip");
  EXPECT(loaded.rootNode().children[0].light->kind ==
             demo::LightKind::Directional,
         "directional kind should round trip");
  EXPECT(loaded.rootNode().children[0].light->shadowStrength == 0.45f,
         "default shadow strength should round trip");
  EXPECT(loaded.rootNode().children[0].light->shadowDistance == 80.0f,
         "default shadow distance should round trip");
  EXPECT(loaded.rootNode().children[0].light->shadowCascadeCount ==
             LX_core::MaxShadowCascades,
         "default shadow cascade count should round trip");
  EXPECT(loaded.rootNode().children[1].light->kind == demo::LightKind::Point &&
             loaded.rootNode().children[1].light->range == 6.0f,
         "point light range should round trip");
  EXPECT(loaded.rootNode().children[2].light->kind == demo::LightKind::Spot &&
             loaded.rootNode().children[2].light->outerConeDegrees == 35.0f,
         "spot cone should round trip");
}

void testSceneDocumentRoundTripsOfflineRenderProfilesAndOfflineSubtrees() {
  const std::filesystem::path path =
      makeTempPath("lx_scene_document_offline_render.yaml");

  std::ofstream out(path);
  out << "scene:\n"
         "  name: offline profile scene\n"
         "  gameplayCameraPath: /game_cam\n"
         "  environment:\n"
         "    enabled: true\n"
         "    hdrUri: cache://polyhaven/studio_small_03/2k-hdr/converted/environment.exr\n"
         "  offlineRender:\n"
         "    defaultProfile: reference\n"
         "    profiles:\n"
         "      preview:\n"
         "        backend: vulkan-compute\n"
         "        integrator: primary-ray\n"
         "        width: 512\n"
         "        height: 512\n"
         "        samples: 1\n"
         "        maxDepth: 1\n"
         "        seed: 1\n"
         "        outputFormat: exr-png\n"
         "      reference:\n"
         "        backend: vulkan-compute\n"
         "        integrator: path-tracing\n"
         "        width: 1920\n"
         "        height: 1080\n"
         "        samples: 64\n"
         "        maxDepth: 4\n"
         "        seed: 7\n"
         "        outputFormat: exr-png\n"
         "        futureField:\n"
         "          enabled: true\n"
         "root:\n"
         "  nodeName: scene_root\n"
         "  name: ''\n"
         "  transform:\n"
         "    translation: [0.0, 0.0, 0.0]\n"
         "    rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "    scale: [1.0, 1.0, 1.0]\n"
         "  visibilityMask: 4294967295\n"
         "  children:\n"
         "    - nodeName: dir_light_node\n"
         "      name: dir_light\n"
         "      transform:\n"
         "        translation: [0.0, 0.0, 0.0]\n"
         "        rotation: [1.0, 0.0, 0.0, 0.0]\n"
         "        scale: [1.0, 1.0, 1.0]\n"
         "      visibilityMask: 4294967295\n"
         "      light:\n"
         "        kind: Directional\n"
         "        direction: [-0.3, -1.0, -0.5]\n"
         "        color: [1.0, 0.98, 0.9]\n"
         "        intensity: 1.0\n"
         "        offline:\n"
         "          sampleWeight: 1.0\n";
  out.close();

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.hasOfflineRenderProfiles(),
         "offlineRender profiles should load");
  const auto &profiles = doc.offlineRenderProfiles();
  EXPECT(profiles.defaultProfile == "reference",
         "default offline profile should load");
  EXPECT(profiles.profiles.at("reference").integrator == "path-tracing",
         "reference integrator should load");
  EXPECT(profiles.profiles.at("reference").samples == 64u,
         "reference samples should load");
  EXPECT(profiles.profiles.at("reference").extensionYamlByField.count(
             "futureField") == 1,
         "unknown profile fields should be retained explicitly");
  EXPECT(doc.environment().hdrUri.find("cache://polyhaven/") == 0,
         "cache URI should remain a URI string");

  const auto *light = findChildByNodeName(doc.rootNode(), "dir_light_node");
  EXPECT(light != nullptr && light->light.has_value(),
         "light node should load");
  EXPECT(light != nullptr && light->light->offlineYaml.has_value(),
         "light offline subtree should be retained");

  const std::filesystem::path savedPath =
      makeTempPath("lx_scene_document_offline_render_saved.yaml");
  demo::saveSceneDocument(savedPath, doc);
  const std::string savedText = readFile(savedPath);
  EXPECT(savedText.find("offlineRender:") != std::string::npos,
         "offlineRender should save");
  EXPECT(savedText.find("futureField:") != std::string::npos,
         "unknown profile extension should save");
  EXPECT(savedText.find("sampleWeight") != std::string::npos,
         "object offline subtree should save");
  EXPECT(savedText.find("cache://polyhaven/studio_small_03") !=
             std::string::npos,
         "cache URI should save without expansion");
}

void testShadowTutorialSceneAssetLoads() {
  const std::filesystem::path path = std::filesystem::current_path() /
                                     "assets" / "scenes" /
                                     "shadow_tutorial.scene.yaml";
  EXPECT(std::filesystem::exists(path),
         "shadow tutorial scene asset should exist");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.sceneName() == "Shadow Tutorial",
         "shadow tutorial scene name should load");
  EXPECT(doc.gameplayCameraPath() == "/game_cam",
         "shadow tutorial gameplay camera should load");
  const auto *camera = findChildByNodeName(doc.rootNode(), "game_camera");
  const auto *receiver = findChildByNodeName(doc.rootNode(), "shadow_receiver");
  const auto *caster = findChildByNodeName(doc.rootNode(), "shadow_caster");
  const auto *light = findChildByNodeName(doc.rootNode(), "dir_light_node");
  EXPECT(camera != nullptr && camera->camera.has_value(),
         "shadow tutorial should contain a fixed camera");
  EXPECT(receiver != nullptr && receiver->meshUri.has_value() &&
             receiver->materialUri.has_value(),
         "shadow tutorial should contain a materialized receiver");
  EXPECT(caster != nullptr && caster->meshUri.has_value() &&
             caster->materialUri.has_value(),
         "shadow tutorial should contain a materialized caster");
  EXPECT(light != nullptr && light->light.has_value() &&
             light->light->kind == demo::LightKind::Directional,
         "shadow tutorial should contain a directional light");
  if (light != nullptr && light->light.has_value()) {
    EXPECT(light->light->shadowStrength == 0.7f,
           "shadow tutorial light should load shadow strength");
    EXPECT(light->light->shadowCascadeCount == 4u,
           "shadow tutorial light should load cascade count");
  }
}

void testIblMetalSphereSceneAssetLoads() {
  const std::filesystem::path path = std::filesystem::current_path() /
                                     "assets" / "scenes" /
                                     "ibl_metal_sphere.scene.yaml";
  EXPECT(std::filesystem::exists(path),
         "IBL metal sphere scene asset should exist");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const demo::SceneDocument doc = demo::loadSceneDocument(path);
  EXPECT(doc.sceneName() == "IBL Metal Sphere",
         "IBL metal sphere scene name should load");
  EXPECT(doc.gameplayCameraPath() == "/game_cam",
         "IBL metal sphere gameplay camera should load");
  EXPECT(doc.hasEnvironment(),
         "IBL metal sphere should declare scene environment");
  if (doc.hasEnvironment()) {
    EXPECT(doc.environment().enabled,
           "IBL metal sphere environment should be enabled");
    EXPECT(doc.environment().hdrUri == "assets/env/studio_small_03_2k.hdr",
           "IBL metal sphere should reference the studio HDR asset");
    EXPECT(doc.environment().skyboxEnabled,
           "IBL metal sphere should request skybox preview");
  }

  const auto *camera = findChildByNodeName(doc.rootNode(), "game_camera");
  const auto *ground = findChildByNodeName(doc.rootNode(), "ibl_ground");
  const auto *sphere = findChildByNodeName(doc.rootNode(), "ibl_metal_sphere");
  const auto *light = findChildByNodeName(doc.rootNode(), "dir_light_node");
  EXPECT(camera != nullptr && camera->camera.has_value(),
         "IBL metal sphere should contain a fixed camera");
  EXPECT(ground != nullptr && ground->meshUri.has_value() &&
             *ground->meshUri == "builtin://lxe_editor/primitives/plane",
         "IBL metal sphere should contain a ground reference plane");
  EXPECT(sphere != nullptr && sphere->meshUri.has_value() &&
             *sphere->meshUri == "builtin://lxe_editor/primitives/sphere",
         "IBL metal sphere should use the builtin sphere primitive");
  EXPECT(sphere != nullptr && sphere->materialUri.has_value() &&
             *sphere->materialUri == "assets/materials/pbr_gold.material",
         "IBL metal sphere should use the PBR gold material");
  EXPECT(light != nullptr && light->light.has_value() &&
             light->light->kind == demo::LightKind::Directional,
         "IBL metal sphere should contain a directional light");
  EXPECT(doc.hasOfflineRenderProfiles(),
         "IBL metal sphere should declare offline render profiles");
  if (doc.hasOfflineRenderProfiles()) {
    EXPECT(doc.offlineRenderProfiles().profiles.count("mvp") == 1,
           "IBL metal sphere should include an mvp offline profile");
  }
}

void testSaveSceneDocumentRejectsNonCanonicalRootIdentity() {
  demo::SceneDocument doc;
  doc.setSceneName("lxe_editor");
  auto &root = doc.mutableRootNode();
  root.nodeName = "custom_root";
  root.name = "root";

  const std::filesystem::path path =
      makeTempPath("lx_scene_document_bad_save_identity.yaml");

  bool threw = false;
  try {
    demo::saveSceneDocument(path, doc);
  } catch (const std::runtime_error &error) {
    const std::string_view message(error.what());
    threw = message.find("root") != std::string_view::npos &&
            message.find("identity") != std::string_view::npos;
  }

  EXPECT(threw, "save should reject non-canonical explicit-root identity");
}

} // namespace

int main() {
  testLoadExplicitRootSceneDocumentReadsGameAndEditorCamera();
  testLoadLegacySceneDocumentNormalizesUnderExplicitRoot();
  testLoadLegacySceneDocumentNormalizesNodeNameBasedParentPaths();
  testLoadMalformedExplicitRootDocumentFailsClearly();
  testLoadExplicitRootDocumentRejectsUnsupportedRootPayload();
  testLoadExplicitRootDocumentRejectsNonCanonicalRootIdentity();
  testSaveSceneDocumentWritesExplicitRootCanonicalFormat();
  testSceneDocumentRoundTripsTypedLightPayloads();
  testSceneDocumentRoundTripsOfflineRenderProfilesAndOfflineSubtrees();
  testShadowTutorialSceneAssetLoads();
  testIblMetalSphereSceneAssetLoads();
  testSaveSceneDocumentRejectsUnsupportedRootPayload();
  testSaveSceneDocumentRejectsNonCanonicalRootIdentity();

  if (failures != 0) {
    std::cerr << "test_scene_document failed with " << failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "test_scene_document passed\n";
  return 0;
}
