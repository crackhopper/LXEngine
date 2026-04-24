## Why

Phase 1 之后缺一个"人工调试默认入口"。`EngineLoop` / `VulkanRenderer` / `GLTFLoader` / Orbit+FreeFly / ImGui overlay / `debug_ui` helper 都已落地，但当前只有 `src/main.cpp = int main(){return 0;}` 和 `src/test/test_render_triangle.cpp` 这个半官方串通测试在用这些能力。`test_render_triangle.cpp` 仍是测试程序、用手写三角形、用 legacy `RenderableSubMesh`、没有 README、没有 UI。本变更把这些已有能力收敛成一个正式的 `src/demos/scene_viewer/`，以 `DamagedHelmet` 作为默认场景，作为后续 Sponza / Shadow / IBL / PostProcess 的 playground 基底。

## What Changes

- 新建目录 `src/demos/` 与 `src/demos/scene_viewer/`（后续 demo 都放 `src/demos/`，不进 `src/test/`）
- 新建 `src/demos/CMakeLists.txt`（`add_subdirectory(scene_viewer)`）
- 新建 `src/demos/scene_viewer/CMakeLists.txt`：定义可执行 `demo_scene_viewer`，链接 `${CORE_LIB}` / `${INFRA_LIB}` / `${GRAPHICS_LIB}` + `imgui`（用于 UI 回调中的 `ImGui::*`）
- 顶层 `CMakeLists.txt` 加 `option(LX_BUILD_DEMOS "Build demo executables" ON)` 并在 ON 时 `add_subdirectory(src/demos)`
- 新建 `src/demos/scene_viewer/main.cpp`：基于 `EngineLoop::run()` 运行（不手写裸主循环）；`cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")` → `LX_infra::Window` → `LX_core::backend::VulkanRenderer` → `Scene` 构建 → `EngineLoop::initialize/startScene/setUpdateHook/run`
- 新建 `src/demos/scene_viewer/scene_builder.{hpp,cpp}`（demo-local glue，不下沉 `infra/`）：`buildHelmetNode(gltfPath) -> SceneNodeSharedPtr`、`buildGroundNode() -> SceneNodeSharedPtr`；内部 `buildMeshFromGltf(loader)` 把 `GLTFLoader` 的 POSITION/NORMAL/TEXCOORD_0/[TANGENT]/indices 拼成 `VertexPosNormalUvBone` buffer + `Mesh`；`makeHelmetMaterial(glTFPbrMaterial, gltfDir)` 基于 `loadGenericMaterial("assets/assets/materials/blinnphong_default.material")` 过渡桥接 baseColorTexture 到 `albedoMap` binding 并设 `enableAlbedo=1`；`enableNormal=0`（DamagedHelmet.gltf 无 TANGENT）
- 新建 `src/demos/scene_viewer/camera_rig.{hpp,cpp}`：封装 Orbit/FreeFly 控制器 + `F2` 边沿切换 + `camera.updateMatrices()` 调用；复用 `test_render_triangle.cpp` 里的 `syncOrbitFromCamera` / `syncFreeFlyFromCamera` 同步逻辑（移到此处）
- 新建 `src/demos/scene_viewer/ui_overlay.{hpp,cpp}`：通过 `VulkanRenderer::setDrawUiCallback` 注入的 UI 绘制函数；Render Stats / Camera / Directional Light panel 复用 `LX_infra::debug_ui::renderStatsPanel / cameraPanel / directionalLightPanel`；Help panel 为 demo-local，`F1` 切换显示
- 新建 `src/demos/scene_viewer/README.md`：demo 目的、前置需求、构建与运行方式（`cmake --build build --target demo_scene_viewer && ./build/src/demos/scene_viewer/demo_scene_viewer`，含 SDL3 LD_LIBRARY_PATH 提示）、控制说明、已知限制
- demo 默认场景：`DamagedHelmet`（SceneNode，主 mesh）+ 地面 plane（SceneNode，手工 quad，20×20 米）+ 默认方向光 + 单相机；`Scene::create("scene_viewer", helmetNode)` 然后 `scene->addChildren(groundNode)`（或等价 API）
- 相机控制：默认 Orbit；`F2` 边沿切换 Orbit↔FreeFly；每帧 update hook 调 `controller.update(camera, input, dt)` → `camera.updateMatrices()` → `input->nextFrame()`
- 本 REQ 不动 `src/main.cpp`（保持占位不阻塞）；不删 `test_render_triangle.cpp`（它继续作为测试存在）

## Capabilities

### New Capabilities

- `demo-scene-viewer`: 项目首个正式 demo 的运行骨架 (基于 `EngineLoop`)、默认场景 (DamagedHelmet + 地面 + 方向光)、相机切换策略 (F2 边沿)、UI overlay 复用 (`debug_ui` + demo-local Help panel)、glTF → 现有 Blinn-Phong 材质的 demo glue 层、`src/demos/` 目录约定、`LX_BUILD_DEMOS` CMake 开关

### Modified Capabilities

- 无（只在 `src/demos/` 下新增；不触碰既有 capability 的 spec 行为）

## Impact

- **代码**：
  - 新增 `src/demos/CMakeLists.txt`、`src/demos/scene_viewer/CMakeLists.txt`、`src/demos/scene_viewer/main.cpp`、`src/demos/scene_viewer/scene_builder.{hpp,cpp}`、`src/demos/scene_viewer/camera_rig.{hpp,cpp}`、`src/demos/scene_viewer/ui_overlay.{hpp,cpp}`、`src/demos/scene_viewer/README.md`
  - 顶层 `CMakeLists.txt` 加 `LX_BUILD_DEMOS` option + `add_subdirectory(src/demos)`
- **构建**：`LX_BUILD_DEMOS=ON`（默认）时产出 `demo_scene_viewer` 可执行；不注册到 ctest
- **测试**：无；REQ 明确不要求自动化测试，人工验收清单见 spec
- **依赖**：REQ-010（资产目录 + `cdToWhereAssetsExist`）、REQ-011（`GLTFLoader` 真实实现）、REQ-012/013（输入）、REQ-014（`Clock`）、REQ-015/016（Orbit / FreeFly）、REQ-017（`VulkanRenderer` ImGui overlay）、REQ-018（`LX_infra::debug_ui` helper，已落地，首选复用）、REQ-020（`EngineLoop`，已落地）
- **下游**：Sponza 场景扩展、shadow / IBL / post-process 集成、完整 PBR 材质桥接；本 REQ 的 glue 层在后续专门材质 / scene graph REQ 落地后可收敛
- **非目标**：自动化测试、完整 PBR 材质系统、Sponza 加载、scene 序列化、热重载、编辑器框架、GLFW 主线验收（SDL 为主，GLFW 路径不在本 REQ 范围）
