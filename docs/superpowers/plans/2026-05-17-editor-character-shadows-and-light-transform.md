# Editor Character Shadows And Light Transform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `Blocky_Character_A`-style textured character materials participate in a visible `Shadow` pass, and make editor transform/gizmo commands update runtime light direction or position.

**Architecture:** Add a first editor-facing projected-shadow pass that draws shadow-caster geometry flattened onto the ground plane with alpha blending; this reuses the existing material pass and frame-graph contracts without introducing offscreen shadow maps. Keep light-node transforms authoritative for spatial light behavior: directional and spot lights derive direction from world rotation, while point and spot positions continue to derive from world translation through `SceneLightsUBO`.

**Tech Stack:** C++20, CMake/Ninja, Vulkan frame graph, GLSL shaders, yaml-cpp material loader, LXEngine command bus and integration tests.

---

## File Structure

- `src/test/integration/test_command_bus.cpp`: command-level regression tests for transform-driven light sync.
- `src/test/integration/test_viewport_overlay.cpp`: gizmo commit regression test that proves rotate commits update a light direction through the existing command path.
- `src/test/integration/test_generic_material_loader.cpp`: material loader regression test for the new projected-shadow pass material shape.
- `src/test/integration/test_frame_graph.cpp`: frame-graph regression test that `Shadow` pass queues include enabled character casters.
- `src/core/editor/commands/builtin_commands.cpp`: transform commands update attached light runtime fields after node transform mutation.
- `src/core/editor/viewport_overlay.cpp`: no behavior change expected beyond tests; gizmo already dispatches `rotate` / `move`.
- `src/core/scene/scene.cpp`: derive directional and spot UBO directions from attached node world rotation; point and spot positions already use world translation.
- `src/core/scene/light.cpp`: allow directional / spot explicit direction setters to continue serving Inspector and YAML, but scene-level UBO uses transform when attached.
- `assets/shaders/glsl/projected_shadow_0.vert`: new projected-shadow vertex shader.
- `assets/shaders/glsl/projected_shadow_0.frag`: new translucent black fragment shader.
- `assets/materials/blinnphong_textured.material`: add `Shadow` pass using `projected_shadow_0`; this is the character model default material.

## Tasks

### Task 1: Transform Commands Sync Attached Light Runtime State

**Files:**
- Modify: `src/test/integration/test_command_bus.cpp`
- Modify: `src/core/editor/commands/builtin_commands.cpp`
- Modify: `src/core/scene/scene.cpp`

- [ ] **Step 1: Write the failing command-bus tests**

Add tests near existing light command tests:

```cpp
void testTransformCommandsDriveAttachedLightSpatialState() {
  CommandFixture fixture;

  const auto addDirectional =
      fixture.bus.dispatch("add light:directional key_light");
  EXPECT(addDirectional.ok, "add directional light succeeds");
  auto *dirNode = fixture.scene->findByPath("/key_light");
  EXPECT(dirNode != nullptr, "directional light node exists");
  const auto dirLight = fixture.scene->getDirectionalLight(*dirNode);
  EXPECT(dirLight != nullptr, "directional light runtime exists");

  const auto rotateDir = fixture.bus.dispatch("rotate /key_light 0 90 0");
  EXPECT(rotateDir.ok, "rotate directional light succeeds");
  const auto dirResources =
      fixture.scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
  (void)dirResources;
  const auto sceneLights = fixture.scene->getSceneLightsUBO();
  const Vec4f dir = sceneLights->param.directional[1].direction;
  EXPECT(nearlyEqual(dir.x, -1.0f) && nearlyEqual(dir.y, 0.0f) &&
             nearlyEqual(dir.z, 0.0f),
         "directional light UBO direction follows node rotation");

  const auto addPoint = fixture.bus.dispatch("add light:point fill_point");
  EXPECT(addPoint.ok, "add point light succeeds");
  auto *pointNode = fixture.scene->findByPath("/fill_point");
  EXPECT(pointNode != nullptr, "point light node exists");
  const auto movePoint = fixture.bus.dispatch("move /fill_point 3 4 5");
  EXPECT(movePoint.ok, "move point light succeeds");
  (void)fixture.scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
  const Vec4f point = sceneLights->param.point[0].positionRange;
  EXPECT(nearlyEqual(point.x, 3.0f) && nearlyEqual(point.y, 4.0f) &&
             nearlyEqual(point.z, 5.0f),
         "point light UBO position follows node translation");
}
```

- [ ] **Step 2: Run test and verify RED**

Run:

```bash
ninja -C build test_command_bus
./build/src/test/integration/test_command_bus
```

Expected: FAIL on `directional light UBO direction follows node rotation`.

- [ ] **Step 3: Implement transform-to-light direction**

Add a helper in `src/core/scene/scene.cpp`:

```cpp
[[nodiscard]] Vec3f lightForwardDirectionFromNode(const SceneNodeSharedPtr &node,
                                                  const Vec3f &fallback) {
  if (!node) {
    return fallback;
  }
  const Transform world = Transform::fromMat4(node->getWorldTransform());
  Vec3f direction = world.rotation.rotate(Vec3f{0.0f, 0.0f, -1.0f});
  if (direction.length2() <= 1e-6f) {
    return fallback;
  }
  return direction.normalized();
}
```

Use it when filling `SceneLightsUBO` directional and spot entries:

```cpp
const auto node = directionalLight->getSceneNode();
const Vec3f direction =
    lightForwardDirectionFromNode(node, directionalLight->getDirection());
```

and:

```cpp
const Vec3f direction =
    lightForwardDirectionFromNode(node, spotLight->getDirection());
```

- [ ] **Step 4: Run command-bus test and verify GREEN**

Run:

```bash
ninja -C build test_command_bus
./build/src/test/integration/test_command_bus
```

Expected: PASS.

### Task 2: Gizmo Rotate Uses The Same Light-Sync Path

**Files:**
- Modify: `src/test/integration/test_viewport_overlay.cpp`

- [ ] **Step 1: Write the failing viewport/gizmo test**

Add a focused test after `testViewportOverlayGizmoModeHotkeysAndCommitPath()`:

```cpp
void testViewportOverlayRotateCommitDrivesDirectionalLight() {
  Fixture fixture;
  auto lightNode = SceneNode::create("key_light");
  lightNode->setName("key_light");
  fixture.scene->addRenderable(lightNode);
  auto light = std::make_shared<DirectionalLight>();
  fixture.scene->attachLight(lightNode, light);

  LX_core::ViewportOverlay overlay(fixture.bus, fixture.editorState,
                                   *fixture.scene);
  overlay.setGizmoOperation(LX_core::ViewportOverlay::GizmoOperation::Rotate);

  LX_core::GizmoTransformComponents components;
  components.rotationEulerDegrees = {0.0f, 90.0f, 0.0f};
  const auto result = overlay.dispatchGizmoCommit("/key_light", components);
  EXPECT(result.ok, "rotate gizmo commit succeeds for light");
  (void)fixture.scene->getSceneLevelResources(Pass_Forward, RenderTarget{});
  const Vec4f dir = fixture.scene->getSceneLightsUBO()->param.directional[1].direction;
  EXPECT(nearlyEqual(dir.x, -1.0f) && nearlyEqual(dir.y, 0.0f) &&
             nearlyEqual(dir.z, 0.0f),
         "gizmo rotate commit updates directional light direction");
}
```

Call it from `main()`.

- [ ] **Step 2: Run test and verify RED or GREEN through Task 1**

Run:

```bash
ninja -C build test_viewport_overlay
./build/src/test/integration/test_viewport_overlay
```

Expected after Task 1: PASS. If run before Task 1: FAIL on direction.

### Task 3: Character Material Defines A Shadow-Caster Pass

**Files:**
- Create: `assets/shaders/glsl/projected_shadow_0.vert`
- Create: `assets/shaders/glsl/projected_shadow_0.frag`
- Modify: `assets/materials/blinnphong_textured.material`
- Modify: `src/test/integration/test_generic_material_loader.cpp`

- [ ] **Step 1: Write failing material loader test**

Add:

```cpp
void test_textured_character_material_has_projected_shadow_pass() {
  auto root = findProjectRoot();
  REQUIRE(!root.empty());
  auto prev = fs::current_path();
  fs::current_path(root);
  auto mat = loadGenericMaterial("assets/materials/blinnphong_textured.material");
  fs::current_path(prev);

  REQUIRE(mat != nullptr);
  REQUIRE(mat->isPassEnabled(Pass_Shadow));
  REQUIRE(mat->getPassShader(Pass_Shadow) != nullptr);
  REQUIRE(mat->getPassShader(Pass_Shadow)->getShaderName() ==
          "projected_shadow_0");
  REQUIRE(mat->getPassRenderState(Pass_Shadow).blendEnable);
  REQUIRE(!mat->getPassRenderState(Pass_Shadow).depthWriteEnable);
}
```

- [ ] **Step 2: Run test and verify RED**

Run:

```bash
ninja -C build test_generic_material_loader
./build/src/test/integration/test_generic_material_loader
```

Expected: FAIL because `Pass_Shadow` is undefined or shader files are missing.

- [ ] **Step 3: Add projected-shadow shaders**

`assets/shaders/glsl/projected_shadow_0.vert`:

```glsl
#version 450

layout(push_constant) uniform ObjectPC {
    mat4 model;
} object;

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 eyePos;
} camera;

layout(set = 0, binding = 0) uniform LightUBO {
    vec4 dir;
    vec4 color;
} sceneLight;

layout(location = 0) in vec3 inPosition;

void main() {
    const float planeY = 0.0125;
    vec3 worldPos = (object.model * vec4(inPosition, 1.0)).xyz;
    vec3 lightDir = normalize(sceneLight.dir.xyz);
    float denom = abs(lightDir.y) < 0.001 ? -0.001 : lightDir.y;
    float t = (planeY - worldPos.y) / denom;
    vec3 projected = worldPos + lightDir * max(t, 0.0);
    projected.y = planeY;
    gl_Position = camera.proj * camera.view * vec4(projected, 1.0);
}
```

`assets/shaders/glsl/projected_shadow_0.frag`:

```glsl
#version 450

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0, 0.0, 0.0, 0.32);
}
```

- [ ] **Step 4: Add Shadow pass to textured material**

Add under `passes:` in `assets/materials/blinnphong_textured.material`:

```yaml
  Shadow:
    shader: projected_shadow_0
    variants: {}
    renderState:
      cullMode: None
      depthTest: true
      depthWrite: false
      blendEnable: true
```

- [ ] **Step 5: Run material loader test and shader compile target**

Run:

```bash
ninja -C build test_generic_material_loader
./build/src/test/integration/test_generic_material_loader
ninja -C build CompileShaders
```

Expected: PASS.

### Task 4: FrameGraph Submits Forward Before Shadow Overlay

**Files:**
- Modify: `src/backend/vulkan/vulkan_renderer.cpp`
- Modify: `src/test/integration/test_frame_graph.cpp`

- [ ] **Step 1: Write failing frame-graph ordering test**

Add:

```cpp
void testEditorProjectedShadowPassKeepsCharacterCaster() {
  auto caster = makeRenderable("shadow_character", {}, true);
  auto scene = Scene::create(caster);
  scene->addCamera(LX_test::makeDefaultCameraNodeWithTarget());
  auto light = makeLightWithPasses({Pass_Forward, Pass_Shadow});
  scene->addLight(light);

  FrameGraph fg;
  fg.addPass(FramePass{Pass_Forward, RenderTarget{}, {}});
  fg.addPass(FramePass{Pass_Shadow, RenderTarget{}, {}});
  fg.buildFromScene(*scene);

  EXPECT(fg.getPasses()[0].name == Pass_Forward,
         "Forward pass renders before projected shadows");
  EXPECT(fg.getPasses()[1].name == Pass_Shadow,
         "Shadow pass renders as overlay after Forward");
  EXPECT(fg.getPasses()[1].queue.getItems().size() == 1,
         "character caster appears in Shadow queue");
}
```

- [ ] **Step 2: Run test**

Run:

```bash
ninja -C build test_frame_graph
./build/src/test/integration/test_frame_graph
```

Expected: PASS for core frame graph; renderer wiring is validated by code review and smoke tests.

- [ ] **Step 3: Wire renderer pass order**

Change `VulkanRendererImpl::initScene()` from `Forward, DebugOverlay` to:

```cpp
m_frameGraph.addPass(
    LX_core::FramePass{LX_core::Pass_Forward, swapchainTarget, {}});
m_frameGraph.addPass(
    LX_core::FramePass{LX_core::Pass_Shadow, swapchainTarget, {}});
m_frameGraph.addPass(
    LX_core::FramePass{LX_core::Pass_DebugOverlay, swapchainTarget, {}});
```

- [ ] **Step 4: Run focused tests**

Run:

```bash
ninja -C build test_frame_graph test_generic_material_loader
./build/src/test/integration/test_frame_graph
./build/src/test/integration/test_generic_material_loader
```

Expected: PASS.

### Task 5: Final Verification

**Files:**
- No new files.

- [ ] **Step 1: Build editor and focused tests**

Run:

```bash
ninja -C build lxe_editor test_command_bus test_viewport_overlay test_generic_material_loader test_frame_graph CompileShaders
```

Expected: all targets build.

- [ ] **Step 2: Run focused integration tests**

Run:

```bash
./build/src/test/integration/test_command_bus
./build/src/test/integration/test_viewport_overlay
./build/src/test/integration/test_generic_material_loader
./build/src/test/integration/test_frame_graph
```

Expected: all print `[PASS]`.

- [ ] **Step 3: Manual editor check when MCP is available**

Run via editor command bus:

```text
scene open <scene-with-Blocky_Character_A>
set /dir_light.light.castsShadow true
select /Blocky_Character_A
preview on
```

Expected: `Blocky_Character_A` draws a visible projected shadow on the plane; rotating `/dir_light` with the rotate gizmo changes shadow direction.

## Self-Review

- Spec coverage: material multi-pass, frame-graph pass filtering, and command/gizmo light sync are covered. Full shadow-map depth rendering is explicitly out of this first fix and remains future work.
- Placeholder scan: no task contains unresolved placeholders.
- Type consistency: tasks consistently use `Pass_Shadow`, `DirectionalLight`, `PointLight`, `SceneLightsUBO`, `RenderState::blendEnable`, and existing command strings.
