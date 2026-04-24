## 1. CMake 骨架

- [x] 1.1 顶层 `CMakeLists.txt` 加 `option(LX_BUILD_DEMOS "Build demo executables" ON)` 并在 `ON` 时 `add_subdirectory(src/demos)`
- [x] 1.2 新建 `src/demos/CMakeLists.txt`：`add_subdirectory(scene_viewer)`
- [x] 1.3 新建 `src/demos/scene_viewer/CMakeLists.txt`：`add_executable(demo_scene_viewer main.cpp scene_builder.cpp camera_rig.cpp ui_overlay.cpp)`；链接 `${CORE_LIB}` / `${INFRA_LIB}` / `${GRAPHICS_LIB}` / `imgui`；`target_include_directories(... PRIVATE ${CMAKE_SOURCE_DIR}/src)`；`add_dependencies(... CompileShaders)`

## 2. scene_builder（glTF → 现有材质 glue）

- [x] 2.1 新建 `src/demos/scene_viewer/scene_builder.hpp`：声明 `buildHelmetNode(path)` / `buildGroundNode()`；内部 glue 放匿名 namespace
- [x] 2.2 新建 `src/demos/scene_viewer/scene_builder.cpp` 实现 `buildMeshFromGltf(loader)`：按 `VertexPosNormalUvBone` 拼顶点，TANGENT 缺失时占位 `{1,0,0,1}` + 一次性 warning
- [x] 2.3 实现 `makeHelmetMaterial(pbrMat, gltfDir)`：`loadGenericMaterial("materials/blinnphong_default.material")` → `setInt("enableNormal", 0)` → baseColor 贴图成功加载时 `setTexture("albedoMap", sampler)` + `setInt("enableAlbedo", 1)`，失败时回退 flat color；`syncGpuData()`
- [x] 2.4 实现 `buildHelmetNode(gltfPath)`：`GLTFLoader` + Mesh + Material → `SceneNode::create("helmet", mesh, material)`
- [x] 2.5 实现 `buildGroundNode()`：4 顶点 2 三角形 XZ 平面（±10m, y=0）+ `blinnphong_default.material` with `enableAlbedo=0` / `setVec3("baseColor", {0.4, 0.4, 0.45})` → `SceneNode::create("ground", ...)`

## 3. camera_rig（Orbit/FreeFly 切换）

- [x] 3.1 新建 `src/demos/scene_viewer/camera_rig.hpp`：`class CameraRig { enum class Mode { Orbit, FreeFly }; attach / update / currentMode }`
- [x] 3.2 新建 `src/demos/scene_viewer/camera_rig.cpp`：匿名 namespace 放 `clampUnit` / `syncOrbitFromCamera` / `syncFreeFlyFromCamera`（与 `test_render_triangle.cpp` 的同步公式一致）；`update()` 先 F2 边沿检测 + 切换 pose，再调相应控制器，最后 `camera.updateMatrices()`
- [x] 3.3 构造时 Orbit 默认 target `{0,0,0}` / distance `3.0` / yaw/pitch 0；FreeFly 初始位置 `{0,0,3}`、yaw `180°`、pitch `0`

## 4. ui_overlay（ImGui 注入）

- [x] 4.1 新建 `src/demos/scene_viewer/ui_overlay.hpp`：`UiOverlay { attach(clock, camera, light, rig); drawFrame(); handleHotkeys(input); }`；含 `bool m_prevF1Down` + `bool m_helpVisible=true`
- [x] 4.2 新建 `src/demos/scene_viewer/ui_overlay.cpp`：`drawFrame()` 调 `debug_ui::beginPanel/renderStatsPanel/cameraPanel/directionalLightPanel/endPanel`；`m_helpVisible` 时绘 Help panel（F1/F2/Orbit/FreeFly 键位）
- [x] 4.3 `handleHotkeys(input)` 在 F1 边沿翻转 `m_helpVisible`；F2 交给 `CameraRig`

## 5. main.cpp

- [x] 5.1 新建 `src/demos/scene_viewer/main.cpp`：按 spec 启动顺序；`cdToWhereAssetsExist` 失败 `return 1`；其他 fatal 走 `try/catch` `return 2`
- [x] 5.2 `make_shared<VulkanRenderer>` 保留 concrete handle 做 `setDrawUiCallback`，同时 upcast 为 `RendererSharedPtr` 喂给 `EngineLoop::initialize`
- [x] 5.3 `setUpdateHook`：`ui.handleHotkeys(*input)` → 更新 aspect → `rig.update(*input, clock.deltaTime())` → `input->nextFrame()`
- [x] 5.4 `loop.run()` 返回后 `renderer->shutdown()` + `return 0`
- [x] 5.5 必要 include 全部到位（engine_loop、vulkan_renderer、window、scene、filesystem_tools、demo-local 三个文件）

## 6. README

- [x] 6.1 新建 `src/demos/scene_viewer/README.md`：Purpose / Upstream requirements / Build & run / Controls / Known limitations 五段齐全
- [x] 6.2 Known Limitations 列出 Blinn-Phong 非 PBR / DamagedHelmet 无 TANGENT 占位 / ImGui overlay 非 FrameGraph / SDL 主线 / 首版只含 DamagedHelmet
- [x] 6.3 Controls 列全 F1 / F2 / Orbit 鼠标 / FreeFly 鼠标+键盘
- [x] 6.4 Build & Run 具体给出 `cmake --build build --target demo_scene_viewer` + SDL3 LD_LIBRARY_PATH 命令
- [x] 6.5 README 末尾直接复述 7 项手工验收 checklist

## 7. 构建与人工验收

- [x] 7.1 `cmake --build build --target demo_scene_viewer` 通过
- [x] 7.2 `cmake --build build` 全量无回归：`test_input_state` / `test_sdl_input` / `test_imgui_overlay` / `test_debug_ui_smoke` / `test_gltf_loader` / `test_engine_loop` / `test_assets_layout` 全部 PASS
- [ ] 7.3 人工验收 1 启动（headless 环境下已确认 `cdToWhereAssetsExist` 成功 + SDL video device 失败路径是预期 fail-fast；完整窗口启动待用户在有显示环境执行）
- [ ] 7.4 人工验收 2 可见性：DamagedHelmet + 地面（待用户执行）
- [ ] 7.5 人工验收 3 Orbit 左键旋转 / 右键平移 / 滚轮缩放（待用户执行）
- [ ] 7.6 人工验收 4 F2 切换 FreeFly WASD/Space/LShift/LCtrl（待用户执行）
- [ ] 7.7 人工验收 5 Stats / Camera / Light / Help 四面板（待用户执行）
- [ ] 7.8 人工验收 6 Camera/Light 编辑可见变化（待用户执行）
- [ ] 7.9 人工验收 7 关闭窗口无崩溃（待用户执行）

## 8. 收尾

- [x] 8.1 更新 `docs/requirements/019-demo-scene-viewer.md` 的 `## 实施状态`：编译链路 + 全量回归已通过；7 项人工验收在本地图形环境下由用户执行
- [x] 8.2 `src/main.cpp` 保持占位不动；`src/test/test_render_triangle.cpp` 不动（已确认）
