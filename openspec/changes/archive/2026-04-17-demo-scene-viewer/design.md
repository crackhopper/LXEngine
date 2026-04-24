## Context

所有底座都已到位：`EngineLoop` 正式主循环、`VulkanRenderer` overlay、SDL 真实输入、Orbit/FreeFly 控制器、`GLTFLoader`（支持 DamagedHelmet）、`LX_infra::debug_ui`（REQ-018 已落地）。缺的只是"把这些拼在一起的默认 playground 入口"。`src/test/test_render_triangle.cpp` 已经证明链路可跑，但它是测试、用手写三角形、用 legacy `RenderableSubMesh`、没有 UI、没有 README。

REQ-019 对本 demo 明确三件事：
- 使用 `EngineLoop` 而非裸 while
- 主路径使用 `SceneNode`，不推荐 `RenderableSubMesh`
- UI 通过 `VulkanRenderer::setDrawUiCallback` 注入

材质现状：项目当前只有 `blinnphong_0` shader + `blinnphong_default.material`；完整 PBR 材质系统尚未到位。REQ-019 R5 明确允许 demo 内自写少量 glue 把 glTF 数据桥到这条现有材质。`DamagedHelmet.gltf` 未声明 TANGENT（REQ-011 已记录），因此 Blinn-Phong 的 `USE_NORMAL_MAP` 分支不能启用——demo 以 albedo-only 起步。

## Goals / Non-Goals

**Goals:**
- 可运行的 `src/demos/scene_viewer/demo_scene_viewer` 可执行
- `DamagedHelmet` 在视口中可见、受光、可旋转/平移/缩放
- 按 `F2` 切换 Orbit/FreeFly，双模式输入均正常
- UI：Stats / Camera / Light / Help 四块面板
- 修改 Camera / Light 字段后画面可见变化
- `cmake --build build --target demo_scene_viewer` 在 `LX_BUILD_DEMOS=ON` 时产出可执行
- 正常关闭无崩溃

**Non-Goals:**
- 自动化测试（REQ 明确不要求）
- 完整 PBR 材质系统
- Sponza
- Scene 序列化 / 热重载 / 编辑器框架
- GLFW 主线验收
- 把 demo 的 glue 下沉到 `infra/`

## Decisions

### D1: `src/demos/scene_viewer/` 目录布局 + `LX_BUILD_DEMOS` 开关

**选择**：

```
src/demos/
├── CMakeLists.txt                     # add_subdirectory(scene_viewer)
└── scene_viewer/
    ├── CMakeLists.txt                 # add_executable(demo_scene_viewer ...)
    ├── main.cpp                       # EngineLoop 主入口
    ├── scene_builder.{hpp,cpp}        # glTF → Mesh / Material / SceneNode 桥接
    ├── camera_rig.{hpp,cpp}           # Orbit/FreeFly 切换 + sync
    ├── ui_overlay.{hpp,cpp}           # setDrawUiCallback 目标
    └── README.md
```

顶层 CMakeLists.txt 加：

```cmake
option(LX_BUILD_DEMOS "Build demo executables" ON)
if(LX_BUILD_DEMOS)
  add_subdirectory(src/demos)
endif()
```

**替代方案**：
- 把 demo 直接写在 `src/main.cpp` → 破坏"后续 demo 都放同一处"的可扩展性；`main.cpp` 作为占位入口与 demo 职责混合
- 放在 `src/test/` → 违反 REQ R1 明确要求（demo 不进 test）
- 不拆 `scene_builder` / `camera_rig` / `ui_overlay`，全塞 `main.cpp` → REQ 边界约束"main.cpp 应保持可读；glue 过多拆到子文件"

**理由**：与仓库现有 `src/{core,infra,backend,test}` 模式一致；四块 translation unit 的关注点各自内聚，改一处不会污染主入口。

### D2: 主路径 `SceneNode`，不借用 `RenderableSubMesh`

**选择**：helmet 与 ground 都以 `SceneNode::create(name, mesh, materialInstance)` 构造；`Scene::create("scene_viewer", helmetNode)` 后追加 ground。

**替代方案**：沿用 `test_render_triangle.cpp` 的 `RenderableSubMesh` → REQ R6 明确要求"demo 应优先使用 SceneNode；不得把 RenderableSubMesh 写成长期推荐对象模型"；即便初期借用也只能视为临时过渡

**理由**：REQ-024 已把 `RenderableSubMesh` 标为 legacy；新 demo 不应在 legacy 路径再立脚。如果 `SceneNode + Scene` 组合在实现时发现差 API，应在实现阶段快速反馈到上游 REQ，而不是绕回 legacy。

### D3: glTF → Mesh 的 glue：统一到 `VertexPosNormalUvBone`

**选择**：`buildMeshFromGltf(loader)` 按 POSITION[i] / NORMAL[i] / TEXCOORD_0[i] 拼 `VertexPosNormalUvBone`：
- `pos` ← POSITION
- `normal` ← NORMAL（若缺则 {0,1,0}，并打一次 warning）
- `uv` ← TEXCOORD_0（若缺则 {0,0}）
- `tangent` ← 若 glTF 带 TANGENT 则读取；否则占位 `{1, 0, 0, 1}` + 全局 warning
- `boneIDs` ← {0, 0, 0, 0}（非蒙皮）
- `boneWeights` ← {0, 0, 0, 0}（非蒙皮）

indices 直接复制。

**替代方案**：
- 定义新顶点布局 `VertexPosNormalUv` → 需要新材质 / shader 编排；超出 demo 范围
- 计算 tangent（MikkTSpace） → REQ-011 已明确禁止这一职责外扩

**理由**：现有 `blinnphong_0` shader + `blinnphong_default.material` 已基于 `VertexPosNormalUvBone`；复用最低成本。占位 tangent 配合 `enableNormal=0` 不会被 shader 实际读到，safe。

### D4: 材质桥：`blinnphong_default.material` + `enableAlbedo=1` + albedoMap 绑定

**选择**：`makeHelmetMaterial(pbrMat, gltfDir)`：
1. `auto mat = LX_infra::loadGenericMaterial("assets/assets/materials/blinnphong_default.material");`
2. `mat->setInt(StringID("enableAlbedo"), 1);`
3. `mat->setInt(StringID("enableNormal"), 0);`（DamagedHelmet 无 TANGENT，normal map 分支不启用）
4. 若 `pbrMat.baseColorTexture` 非空：`auto tex = 加载 gltfDir / pbrMat.baseColorTexture` → `mat->setTexture(StringID("albedoMap"), combinedSampler);`
5. 其他 PBR texture（metallicRoughness / normal / occlusion / emissive）不绑定，仅在 UI 里以只读 label 展示（供未来 PBR 材质 loader 使用）
6. `mat->syncGpuData();`

**ground** 复用同一份 `.material` 但 `enableAlbedo=0`，只靠 `baseColor` 常量 + 受光颜色。

**替代方案**：
- 绕过 `loadGenericMaterial` 手搓 `MaterialInstance` → 重复 loader 已做的 binding 解析工作
- 启用 normal map → 需要真实 tangent，违反 REQ 约束

**理由**：最小 glue 满足"画面可见差异"验收；完整 PBR 留给下游 REQ。

### D5: Ground plane 手工 quad

**选择**：`buildGroundNode()` 生成 20×20m 的 XZ 平面（y=0），2 三角形 4 顶点，normal={0,1,0}，uv=角点 0~1；复用 `VertexPosNormalUvBone`；material = `blinnphong_default.material` with `enableAlbedo=0`, `baseColor={0.5,0.5,0.5}`。

**替代方案**：不画地面 → REQ R4 明确要求包含"一块地面 plane"

**理由**：手搓 4 顶点 6 索引足够作深度/光照参考；不引入第二份 gltf 资产。

### D6: `F2` 切换 + `F1` Help 边沿检测，写在 demo 本地

**选择**：`camera_rig` 与 `ui_overlay` 各自持有 `bool m_prevF2Down` / `bool m_prevF1Down` 做"上一帧 vs 当前帧"边沿检测；update hook 里先查边沿再更新控制器。

**替代方案**：
- 改 `IInputState` 加 `isKeyPressed()` 边沿 API → REQ 明确说明"`Sdl3InputState` 当前只提供状态量，边沿检测允许在 demo 中手写局部逻辑"；不在本 REQ 扩接口
- 统一封装成 utility → 两个位置、两套状态，直接就地写反而更清楚

**理由**：REQ R7 明确允许 demo-local 边沿检测；不把它升成通用机制避免 API 膨胀。

### D7: Orbit 与 FreeFly 同步沿用 test_render_triangle 实现

**选择**：把 `test_render_triangle.cpp` 里的 `syncOrbitFromCamera` / `syncFreeFlyFromCamera` / `clampUnit` helper 搬到 `camera_rig.cpp` 的匿名命名空间内；`CameraRig::switchTo(mode)` 执行相应同步。

**替代方案**：重写 → 逻辑已跑通验证过，重写没有收益

**理由**：这两个函数就是 REQ-019 隐含依赖的"模式切换保持视角连续性"实现；复用已经通过人工验证。

### D8: UI overlay 首选 `LX_infra::debug_ui`

**选择**：`ui_overlay.cpp` include `infra/gui/debug_ui.hpp`；Stats / Camera / DirectionalLight 三块直接调 `debug_ui::renderStatsPanel / cameraPanel / directionalLightPanel`。Help panel demo-local（4~6 行 `ImGui::Text` + `F1` 边沿开关）。

**替代方案**：
- demo 自写四个 panel → REQ R8 明确"若 REQ-018 已落地，则应优先复用 `LX_infra::debug_ui::*`"；REQ-018 当前已落地
- 省略 Help → REQ R8 明确要求四块 panel 包含 Help

**理由**：`debug_ui` 的三块组合 panel 正好覆盖 Stats / Camera / Light；Help 是 demo 特有的快捷键帮助，应留在 demo-local。

### D9: UI 注入点 `VulkanRenderer::setDrawUiCallback`

**选择**：`main.cpp` 在 `renderer->initialize(...)` 之后、`loop.run()` 之前，把 `ui_overlay::drawFrame(...)` 绑定的 lambda 通过 `static_cast<VulkanRenderer&>(*renderer).setDrawUiCallback(...)` 或直接在 VulkanRenderer 指针上调用。

**替代方案**：`gpu::Renderer` 基类统一 UI callback API → REQ R8 明确"不能假设基类已统一 API"，且 REQ-017 已设计 callback 只在 `VulkanRenderer` 暴露

**理由**：demo 本来就知道自己用的是 `VulkanRenderer`；不跨 abstraction。

### D10: 资产定位顺序 + 失败 fail-fast

**选择**：`main()` 第一步 `if (!cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")) return 1;`；后续所有相对路径（`.material`、glTF、贴图）基于这个 CWD 展开。GLTFLoader / MaterialLoader 的异常不在 demo 里吞——直接 let main 抛出/返回非 0 让 OS 看到失败。

**替代方案**：尝试多个资产路径 / 吞异常 / 使用 placeholder → 遮蔽真实错误，违反 demo 作为"联调入口"的定位

**理由**：早失败 + 保留原始 stack 是调试友好最大的投资。

### D11: `LX_BUILD_DEMOS=ON` 默认开启

**选择**：顶层 CMakeLists.txt 加选项，默认 `ON`。

**替代方案**：默认 `OFF`（避免 CI 构建 demo） → demo 本来就在 `LX_Infra` / `LX_Backend` 之上只加一个可执行链接，增量 ~1s；默认 ON 让 `cmake --build build` 自动把 demo 也构起来，避免"改了 glue 但没发现 demo 链接断了"

**理由**：demo 不进 ctest 但仍必须编译通过；保持默认 ON 让"全量构建"是最强一致性保证。

## Risks / Trade-offs

- **[Blinn-Phong ≠ PBR]** 当前材质桥只做到 albedo，DamagedHelmet 的金属感/粗糙度/环境光遮蔽/自发光不会正确渲染；缓解：UI 面板把未绑定的 PBR 贴图路径以 label 显示出来，让用户清楚"已读取但未使用"；README 已知限制列明
- **[SceneNode 主路径若有 API 差]** 若 `SceneNode + Scene` 的实际组合对本 demo 差一个 API（例如追加多节点的接口），本 REQ 不扩 core；缓解：实现阶段先尝试 `Scene::create(helmetNode)` 后看是否需要添加第二节点的 API，若确实缺失则回到上游 REQ（`scene-node-validation` / 相关 capability）处理，不绕回 `RenderableSubMesh`
- **[DamagedHelmet 缺 TANGENT]** normal map 分支彻底不启用；缓解：REQ-011 spec 与 REQ-019 R5 已接受；后续资产替换或 tangent 生成 REQ 落地即可启用
- **[地面遮挡或 z-fighting]** ground y=0，helmet 通常 y>0；仍保留相机近远裁剪默认 near=0.1 / far=1000；缓解：若首次运行穿模，调 helmet 高度/ground 高度，而不是乱调 near/far
- **[ImGui 键捕获吃掉 F1/F2]** Help panel 在输入框时 ImGui 可能 capture keyboard；缓解：边沿检测发生在 `input->isKeyDown` 基础上，`Sdl3InputState` 会在 `isUiCapturingKeyboard()` 为真时拦截——但 REQ-018 未强制 SDL 接通 WantCaptureKeyboard；若实测干扰，可在 demo 内加 `if (input->isUiCapturingKeyboard()) skipHotkeys();`
- **[LD_LIBRARY_PATH 运行约束]** SDL3 以 vendored 预编译包方式存在，运行需 `LD_LIBRARY_PATH=build/_deps/sdl3-build:...`；缓解：README 明写；未来 RPATH 优化属另立 REQ

## Migration Plan

1. 顶层 `CMakeLists.txt` 加 `LX_BUILD_DEMOS` option + `add_subdirectory(src/demos)`
2. `src/demos/CMakeLists.txt` + `src/demos/scene_viewer/CMakeLists.txt` 建 target
3. `scene_builder.{hpp,cpp}`（glTF 解包 + 材质桥接 + ground helper）
4. `camera_rig.{hpp,cpp}`（Orbit/FreeFly + 切换）
5. `ui_overlay.{hpp,cpp}`（setDrawUiCallback 目标 + Help panel）
6. `main.cpp` 把上面 3 个串起来
7. README
8. 本地构建 + 人工验收（7 项清单）

每一步 build 通过后再进下一步；main.cpp 最后写，避免顶层依赖不完整。

## Open Questions

- `scene_builder::makeHelmetMaterial` 的 binding 名应为 `albedoMap` 还是别名？→ 实现阶段实测 `generic_material_loader` 解析 `blinnphong_default.material` 得到的 binding 名；若不是 `albedoMap` 就按真实名设置，不在 spec 硬编码
- `Scene::create("scene_viewer", helmet)` 之后如何追加 ground 节点？→ 实现阶段先读 `scene.hpp` 的完整 API（看 Scene 是否有 `addRenderable` / `addRoot` 等），若只允许单根，则把 helmet + ground 组合成一个父 `SceneNode` 的子节点；这个调整不改本 spec 的承诺（demo 有 helmet + ground）
