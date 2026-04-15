# REQ-019: demo_scene_viewer 集成 demo

## 背景

REQ-010 至 REQ-018 各自交付了一块基础设施，但没有一个最终的"集成入口"把它们装到一起跑起来。现状的 `src/test/test_render_triangle.cpp:34` 是一个极简的"画一个三角形"的硬编码 demo，**不**适合：

- 加载 REQ-010 的 PBR 资产（写满 OBJ + 没有相机交互）
- 试 REQ-015 / REQ-016 的相机控制器
- 显示 REQ-017 / REQ-018 的 ImGui panel
- 作为 Phase 1 后续 REQ-101+ 的"基底"持续演进

本需求把上面 9 个 REQ 整合到一个**面向人类调试**的 demo —— `demo_scene_viewer`。它的定位：

- **不是** 集成测试（不在 ctest 里跑，不卡 CI）
- **不是** tutorial 示例（不教学，假设读者已经懂引擎 API）
- **是** Phase 1 后续每个新 REQ（REQ-101 ~ REQ-110）的**默认开发入口**：新增一个 pass / 新材质 / 新后期效果，第一站就是把它接进 `demo_scene_viewer` 跑通

REQ-020 引入 `EngineLoop` 之后，本 demo 不再把 `Window + Renderer + while(running)` 直接摊在 `main.cpp` 里，而是作为 `EngineLoop` 的首个完整集成入口。

## 目标

1. 新建 `src/demos/` 顶层目录（与 `src/test/` 平行）
2. `demo_scene_viewer` 加载 DamagedHelmet + 一块地面 plane + 默认 directional light
3. 默认 OrbitCameraController，按 F2 切换 FreeFlyCameraController
4. ImGui debug panel 显示 render stats + camera + light，可编辑参数
5. CMake 注册为独立 target，独立于 ctest
6. 覆盖 REQ-010 ~ REQ-018 所有交付项的最小整合路径

## 依赖

- **REQ-020**（必需）：提供 `EngineLoop` 作为 demo 的运行骨架

## 需求

### R1: `src/demos/` 目录约定

新建：

```
src/demos/
├── CMakeLists.txt
└── scene_viewer/
    ├── CMakeLists.txt
    ├── main.cpp
    └── README.md
```

`README.md` 写明：

- demo 的目的（Phase 1 prereq 集成入口）
- 依赖的 REQ 列表
- 启动方式（命令行 + 环境变量 LX_RENDER_DEBUG）
- 控制说明（鼠标 / 键盘 / F1 / F2 / F4）
- 已知限制

### R2: main 流程

`src/demos/scene_viewer/main.cpp`（不超过 150 行）：

```cpp
int main() {
  // 1. 工作目录
  if (!cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")) {
    std::cerr << "asset not found, run from repo root or build dir\n";
    return 1;
  }

  // 2. window + renderer
  LX_infra::Window::Initialize();
  auto window = std::make_shared<LX_infra::Window>("scene_viewer", 1280, 720);
  auto renderer = std::make_shared<LX_core::backend::VulkanRenderer>(/*token*/{});
  renderer->initialize(window, "scene_viewer");

  // 3. 加载 DamagedHelmet
  GLTFLoader loader;
  loader.load("assets/models/damaged_helmet/DamagedHelmet.gltf");
  auto helmetMesh = buildMeshFromGltf(loader);             // 见 R3

  // 4. 临时材质：把 GLTF baseColor / normal / metallicRoughness 路径喂给现有 BlinnPhong loader
  auto material = LX_infra::loadBlinnPhongMaterial();
  applyGltfTexturesIfAvailable(material, loader.getMaterial());  // 见 R4
  material->updateUBO();

  auto helmetRenderable = std::make_shared<RenderableSubMesh>(
      helmetMesh, material, Skeleton::create({}));

  // 5. 地面 plane（两个三角形，复用 BlinnPhong）
  auto groundRenderable = makeGroundPlane();               // 见 R5

  // 6. 场景
  auto scene = Scene::create(helmetRenderable);
  scene->addRenderable(groundRenderable);

  auto camera = scene->getCameras().front();
  camera->aspect = 1280.0f / 720.0f;
  auto dirLight = std::dynamic_pointer_cast<DirectionalLight>(scene->getLights().front());
  dirLight->ubo->param.dir = {-0.4f, -1.0f, -0.3f, 0.0f};
  dirLight->ubo->param.color = {1.0f, 1.0f, 1.0f, 1.0f};
  dirLight->ubo->setDirty();

  // 7. controllers
  auto orbit = std::make_shared<OrbitCameraController>(Vec3f{0, 0, 0}, 4.0f);
  auto freefly = std::make_shared<FreeFlyCameraController>(Vec3f{0, 1, 4});
  ICameraController* activeCtrl = orbit.get();

  // 8. EngineLoop
  EngineLoop loop;
  loop.initialize(window, renderer);
  loop.startScene(scene);

  // 9. ImGui callback
  bool showHelp = true;
  renderer->setDrawUiCallback([&]() {
    debug_ui::renderStatsPanel(loop.getClock());
    debug_ui::cameraPanel("Camera", *camera);
    debug_ui::directionalLightPanel("Sun", *dirLight);
    if (showHelp) {
      if (debug_ui::beginPanel("Help")) {
        ImGui::TextUnformatted("F1: toggle this help");
        ImGui::TextUnformatted("F2: switch Orbit / FreeFly");
        ImGui::TextUnformatted("RMB drag: look (FreeFly) / pan (Orbit)");
        ImGui::TextUnformatted("WASD + Space/LShift: move (FreeFly)");
        ImGui::TextUnformatted("LMB drag + wheel: orbit (Orbit)");
      }
      debug_ui::endPanel();
    }
  });

  // 10. 业务 update hook
  bool prevF2 = false;
  bool prevF1 = false;
  loop.setUpdateHook([&](Scene& scene, const Clock& clock) {
    auto& input = *window->getInputState();

    // F1 toggle help
    bool curF1 = input.isKeyDown(KeyCode::F1);
    if (curF1 && !prevF1) showHelp = !showHelp;
    prevF1 = curF1;

    // F2 toggle camera mode
    bool curF2 = input.isKeyDown(KeyCode::F2);
    if (curF2 && !prevF2) {
      activeCtrl = (activeCtrl == orbit.get())
                       ? static_cast<ICameraController*>(freefly.get())
                       : orbit.get();
    }
    prevF2 = curF2;

    activeCtrl->update(*camera, input, clock.deltaTime());
    camera->updateMatrices();
  });

  // 11. 运行
  loop.run();
}
```

边沿检测（F1 / F2 toggle）在 demo 里手写，不依赖 REQ-012 的边沿 API（那是 Phase 2）。
`main.cpp` 仍然允许保留少量“展开式”逻辑（例如 GLTF glue code），但主循环的编排必须收敛到 `EngineLoop`，不再手写 `while (...) { uploadData(); draw(); }`。

### R3: `buildMeshFromGltf(loader)` helper

在 `src/demos/scene_viewer/main.cpp` 内（或拆出一个 `gltf_to_mesh.hpp`）：

```cpp
static MeshPtr buildMeshFromGltf(const GLTFLoader& loader) {
  // 把 positions / normals / texCoords / tangents 拼成 VertexPosNormalUvBone
  // bone weight 全置零（DamagedHelmet 没有骨骼）
  // 索引直接复用 loader.getIndices()
  std::vector<VertexPosNormalUvBone> vertices;
  vertices.reserve(loader.getPositions().size());
  for (size_t i = 0; i < loader.getPositions().size(); ++i) {
    vertices.emplace_back(
        loader.getPositions()[i],
        loader.getNormals()[i],
        loader.getTexCoords()[i],
        Vec4f{1, 0, 0, 1}, /* tangent placeholder if not present */
        std::array<int, 4>{0, 0, 0, 0},
        Vec4f{1, 0, 0, 0}
    );
  }
  auto vb = VertexBuffer<VertexPosNormalUvBone>::create(vertices);
  auto ib = IndexBuffer::create(loader.getIndices());
  return Mesh::create(vb, ib);
}
```

如果 REQ-011 实现的 `getTangents()` 返回非空，把第 4 个参数从 placeholder 替换成真实 tangent。

### R4: `applyGltfTexturesIfAvailable(material, gltfMat)` helper

```cpp
static void applyGltfTexturesIfAvailable(MaterialPtr mat,
                                         const GLTFPbrMaterial& gltfMat) {
  // 如果 baseColorTexture 非空 → 加载到 material 的 albedoMap slot
  // metallicRoughness / normal / emissive 同理
  // 没对应 slot 的（比如现在的 BlinnPhong 没有 metallicRoughness）→ skip
  // 用 setInt("enableNormal", 1) 之类的 toggle 启用对应分支
  // 找不到任何贴图时退回 base color factor
}
```

这个 helper 是 REQ-019 的"过渡胶水"，存在的原因是当前材质系统是 BlinnPhong 不是 PBR。一旦 Phase 1 REQ-101+ 引入真正的 PBR material loader，这个 helper 会被替换为 `LX_infra::loadGltfPbrMaterial(loader.getMaterial())`，所以本 REQ 把它放在 demo 内部而不是 `infra/loaders/`，避免污染。

### R5: `makeGroundPlane()` helper

构造一个 10×10 的 quad（两个三角形），法线向上，UV 0..1。复用 BlinnPhong material（不同的 instance，不同贴图或纯色）。提供阴影投射的接收面，未来 REQ-103 直接用。

### R6: CMake target

`src/demos/scene_viewer/CMakeLists.txt`：

```cmake
add_executable(demo_scene_viewer main.cpp)
target_link_libraries(demo_scene_viewer
  PRIVATE
    lx_core
    lx_infra
    lx_backend_vulkan
)
target_compile_features(demo_scene_viewer PRIVATE cxx_std_20)
```

`src/demos/CMakeLists.txt`：

```cmake
add_subdirectory(scene_viewer)
```

顶层 `src/CMakeLists.txt`：

```cmake
add_subdirectory(demos)
```

**不**注册为 ctest（demo 需要窗口 + 用户交互，不适合 CI）。
**不**默认编译：用一个 `LX_BUILD_DEMOS` cmake option 守护，默认 `ON`，可关闭。

### R7: README

`src/demos/scene_viewer/README.md`：

```markdown
# demo_scene_viewer

Phase 1 渲染开发的默认入口 demo。

## 它做什么

- 加载 `assets/models/damaged_helmet/DamagedHelmet.gltf` + 一块地面 plane
- 用 OrbitCamera / FreeFlyCamera 两种风格观察
- ImGui panel 显示 / 编辑 camera + light + render stats

## 控制

| 操作 | 行为 |
|---|---|
| 左键拖（Orbit 模式） | 围绕 helmet 旋转 |
| 右键拖（Orbit 模式） | 平移 target |
| 滚轮（Orbit 模式） | 缩放 |
| WASD（FreeFly 模式） | 前后左右 |
| Space / LShift（FreeFly 模式） | 上 / 下 |
| 右键拖（FreeFly 模式） | 鼠标 look |
| LCtrl 按住（FreeFly 模式） | 加速 |
| F1 | 显示/隐藏帮助 |
| F2 | 在 Orbit / FreeFly 间切换 |

## 启动

\`\`\`bash
cd build
ninja demo_scene_viewer
./src/demos/scene_viewer/demo_scene_viewer
\`\`\`

## 已知限制

- 当前 material 走 BlinnPhong，不是真正的 PBR —— 等 Phase 1 REQ-101+
- ImGui 是 overlay 模式，不在 FrameGraph 里 —— REQ-017 的边界
- 鼠标右键 look 时鼠标不锁定 —— REQ-016 的边界
- 没有 shadow map / IBL —— REQ-103 / REQ-105 / REQ-106 之后
```

## 测试

- **不写自动化测试** —— demo 是人手交互的，集成测试覆盖率由前 9 个 REQ 各自的测试承担
- 验收方式：

  1. 启动 `demo_scene_viewer`，看到窗口、看到 helmet（带贴图）、看到地面
  2. 鼠标左键拖动相机围绕 helmet 旋转
  3. 滚轮缩放
  4. F2 切换到 FreeFly，WASD 漫游不卡
  5. ImGui Render Stats panel 显示 ~60 FPS
  6. 拖动 ImGui Camera panel 的 fovY slider 看到画面变化
  7. 拖动 Sun panel 的方向，看到 helmet 上的高光区域跟着移动
  8. 关闭窗口，无崩溃

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/demos/CMakeLists.txt` | 新增 |
| `src/demos/scene_viewer/CMakeLists.txt` | 新增 |
| `src/demos/scene_viewer/main.cpp` | 新增 |
| `src/demos/scene_viewer/README.md` | 新增 |
| `src/CMakeLists.txt` | 加 `add_subdirectory(demos)` |
| 顶层 `CMakeLists.txt` | 加 `option(LX_BUILD_DEMOS "Build demo executables" ON)` |

## 边界与约束

- **不写** automated test —— demo 是 Phase 1 的人工调试入口，不属于 CI 验证范围
- **不实现** PBR material loader —— 桥接到现有 BlinnPhong；真正的 PBR loader 是后续 REQ
- **不实现** shadow / IBL / bloom —— 留给 REQ-103 ~ REQ-110
- **不引入** scene 序列化 / hot reload —— Phase 3
- demo main 函数 ≤ 150 行，超过的话拆成 helper 函数（同文件内或 `src/demos/scene_viewer/` 子文件）
- 只支持 SDL3 backend —— GLFW backend 走 dummy input，跑起来相机不动

## 依赖

按顺序，必须**全部**已合入：

- **REQ-010**：DamagedHelmet 资产 + `cdToWhereAssetsExist`
- **REQ-011**：`GLTFLoader` 真正能加载 DamagedHelmet
- **REQ-012**：`IInputState` 接口
- **REQ-013**：SDL3 真实输入
- **REQ-014**：`Clock` 提供 deltaTime
- **REQ-015**：`OrbitCameraController`
- **REQ-016**：`FreeFlyCameraController`
- **REQ-017**：ImGui overlay 接入 VulkanRenderer
- **REQ-018**：`debug_ui` helper

任何一个 REQ 缺失 demo 都跑不起来或编译失败 —— 因此本 REQ 是整个前置链的最末端。

## 下游

- **REQ-101 ~ REQ-110**（Phase 1 渲染深度）：每个新 pass / 新材质 / 新后期效果首先在 `demo_scene_viewer` 里跑通再算落地。Sponza 加载 / shadow map / IBL / bloom / FXAA 都会扩展本 demo 的代码
- **Phase 2 REQ-208**：FreeFly 完整版（带 ActionMap + 手柄）会替换 REQ-016 的 controller，本 demo 直接受益
- **Phase 2 REQ-210**：dump API 可以接到本 demo 的 ImGui panel，作为 `describe()` 的第一个消费者

## 实施状态

未开始。
