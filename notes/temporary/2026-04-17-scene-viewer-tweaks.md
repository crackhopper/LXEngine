# `demo_scene_viewer` 调参指南

> 一份"demo 已经跑起来之后，我想改 X"的速查。
> 每一条都给三个信息：**运行时改哪儿**、**代码里改哪儿**、**为什么是这样**。
>
> 对应文件：
> - `src/demos/scene_viewer/main.cpp` — 启动装配
> - `src/demos/scene_viewer/scene_builder.cpp` — 几何 / 材质 glue
> - `src/demos/scene_viewer/camera_rig.cpp` — 相机控制
> - `src/demos/scene_viewer/ui_overlay.cpp` — ImGui 面板
> - `src/infra/gui/debug_ui.cpp` — 面板 helper 的真实实现
> - `src/infra/gui/imgui_gui.cpp` — ImGui 上下文初始化

---

## UI / ImGui

### 调字体大小

**运行时**：目前 demo 没有暴露滑条，所以没有 runtime 捷径。

**代码**：两种加法，按效果和代价选。

**方法 A —— 全局缩放（最省事，像素会糊）**

改 `src/infra/gui/imgui_gui.cpp::Gui::init()`，在 `ImGui::CreateContext()` 之后
加一行：

```cpp
ImGui::CreateContext();
ImGui::StyleColorsDark();
ImGui::GetIO().FontGlobalScale = 1.5f;  // 1.0 = 默认
```

这是把 ImGui 内部所有文字绘制做整体放大 —— 实现上等价于在 draw data 上
乘矩阵。代价是：**原生字体 atlas 是在 1x 像素上栅格化的，放大到
1.5x 视觉会发糊**，尤其是小号字号。如果只是想在 4K 屏上看清，这招够用。

**方法 B —— 加载具体 size 的字体（清晰但要显式提供 ttf）**

同样在 `Gui::init()` 的 `CreateContext` 之后、`ImGui_ImplVulkan_Init` 之前：

```cpp
ImGuiIO& io = ImGui::GetIO();
io.Fonts->Clear();
// 如果你想加载自己的 ttf：
io.Fonts->AddFontFromFileTTF("assets/fonts/NotoSansCJK-Regular.otf",
                             22.0f);
// 或者用 ImGui 自带默认字体但指定大小（默认字体只支持 ASCII）：
// ImFontConfig cfg;
// cfg.SizePixels = 22.0f;
// io.Fonts->AddFontDefault(&cfg);
```

注意：

- ImGui 的 Vulkan backend 会在第一次 `ImGui::Render()` 前懒加载字体 atlas
  到 GPU；如果在 `Gui::init()` 之后才改 atlas，需要重建 ImGui 的 font
  texture（`ImGui_ImplVulkan_CreateFontsTexture()`）。放在 init 里最稳。
- 中文支持要点：`AddFontFromFileTTF` 的第 4 个参数 glyph range 要包含 CJK。
  简单粗暴可用 `io.Fonts->GetGlyphRangesChineseFull()`。字体 atlas 会大一些。
- demo 目前没引入自定义字体文件；`assets/fonts/` 目录要自己建，ttf 自己放。

**为什么 demo 默认不加字体**：REQ-018 明确"不做自定义字体系统"，所以
`debug_ui` helper 不管 font。这是未来单独一个 REQ 的工作。

### 改面板的默认位置 / 大小

**代码**：`src/infra/gui/debug_ui.cpp::beginPanel()`

```cpp
bool beginPanel(const char* title) {
  ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320.0f, 400.0f), ImGuiCond_FirstUseEver);
  return ImGui::Begin(title);
}
```

`ImGuiCond_FirstUseEver` 意味着只在窗口第一次出现时应用这些值；之后用户
拖动/缩放的偏好会被 ImGui 存到 `.ini` 里保留。把常量改大/改位置即可。

想**每次**启动都强制复位位置：把 `ImGuiCond_FirstUseEver` 换成
`ImGuiCond_Always`。

想让每个面板有自己的尺寸：`beginPanel` 里现在是统一默认值，可以改签名接
`const ImVec2& size`，或者给每个 panel 单独写一套 `SetNextWindowSize`。

### 改面板颜色主题

**代码**：`src/infra/gui/imgui_gui.cpp::Gui::init()` 里的 `ImGui::StyleColorsDark()`。
换成 `StyleColorsLight()` 或 `StyleColorsClassic()`，或者手动微调：

```cpp
ImGui::StyleColorsDark();
ImGuiStyle& style = ImGui::GetStyle();
style.FrameRounding = 4.0f;
style.WindowRounding = 6.0f;
style.Colors[ImGuiCol_WindowBg].w = 0.85f; // 半透明
```

---

## 光照

### 改"初始"光照方向和颜色（启动就想要 X）

**代码**：`src/demos/scene_viewer/main.cpp`：

```cpp
auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
    scene->getLights().front());
if (dirLight && dirLight->ubo) {
  dirLight->ubo->param.dir   = LX_core::Vec4f{-0.3f, -1.0f, -0.5f, 0.0f};
  dirLight->ubo->param.color = LX_core::Vec4f{1.0f, 0.98f, 0.9f, 1.0f};
  dirLight->ubo->setDirty();
}
```

要点：

- `dir` 前三分量是**光线传播方向向量**（从光源指向被照射点），不是"光
  来自的方向"。所以 `{0, -1, 0}` = 正顶光向下。shader 里会做
  `L = normalize(-sceneLight.dir.xyz)` 再算 diffuse，所以传入的是"光
  往哪里打"。
- `dir.w` 不参与光照计算（现阶段），但 `Vec4f` 出于 std140 对齐保留着。
- `color` 的 `w` 同样当前不用。rgb 直接乘在 shader 的 diffuse / specular 上；
  如果想提亮可以把 rgb 乘以 2、3，而不是调 `w`。
- **必须调 `setDirty()`**。`VulkanResourceManager::syncResource` 只拷
  dirty 资源；不标脏的话你在 CPU 侧改了的数据永远不会上 GPU。

### 运行时改光照

**运行时**：默认就开着 `Directional Light` 面板 —— 在窗口里拖 `dir` 或
`color`。`debug_ui::directionalLightPanel` 内部在任意 widget 返回
"changed" 时自动调 `light.ubo->setDirty()`，不用手动管。

**代码路径**（`src/infra/gui/debug_ui.cpp`）：

```cpp
void directionalLightPanel(const char* title, DirectionalLight& light) {
  separatorText(title);
  bool changed = false;
  changed |= dragVec4("dir", light.ubo->param.dir, 0.01f);
  changed |= colorEdit4("color", light.ubo->param.color);
  if (changed) {
    light.ubo->setDirty();
  }
}
```

### 想加第二盏光

本 demo 现在只有一盏默认方向光（`Scene` 构造时自动创建）。要加更多
光源：

```cpp
// main.cpp 里 scene 创建之后：
auto extraLight = std::make_shared<LX_core::DirectionalLight>();
extraLight->ubo->param.dir   = LX_core::Vec4f{1.0f, -0.5f, 0.0f, 0.0f};
extraLight->ubo->param.color = LX_core::Vec4f{0.3f, 0.3f, 1.0f, 1.0f};
extraLight->ubo->setDirty();
scene->addLight(extraLight);
```

**注意**：当前 `blinnphong_0` shader 的 `LightUBO` 只有一组 `dir/color`，
多光源不会被自动渲染 —— shader 侧要改成 light array 或用 REQ-029
（多光源 scene resource model，未落地）的路径。demo glue 能塞但 GPU
只看第一盏。

### 想做个"无光照 unlit"预览

材质里把 `USE_LIGHTING` 变体去掉（对应 `blinnphong_default.material`，没
有 `variants:` 段），或者自己起一个新 `.material`。然后 `scene_builder`
里让 helmet 用它。详见 `materials/blinnphong_textured.material` 做模板。

---

## 相机

### 改开场相机位置 / 看向哪

**代码**：`src/demos/scene_viewer/main.cpp`：

```cpp
camera->position = LX_core::Vec3f{2.5f, 1.5f, 3.0f};
camera->target   = LX_core::Vec3f{0.0f, 0.0f, 0.0f};
camera->up       = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
camera->aspect   = static_cast<float>(kWindowWidth) / kWindowHeight;
camera->updateMatrices();
```

右手坐标系，+Y 是上。`updateMatrices()` 读 `position/target/up/fovY/
aspect/near/far` 生成 view+projection UBO —— **不调它，画面用的是旧矩阵。**

### 改 FOV / 近远裁剪

**运行时**：ImGui `Camera` 面板里直接拖 `fovY` / `near` / `far`。

范围定义在 `src/infra/gui/debug_ui.cpp::cameraPanel`：

```cpp
sliderFloat("fovY", camera.fovY, 1.0f, 179.0f);
sliderFloat("aspect", camera.aspect, 0.1f, 4.0f);
sliderFloat("near", camera.nearPlane, 0.001f, 10.0f);
sliderFloat("far", camera.farPlane, 1.0f, 10000.0f);
```

想把 far plane 拉到 50000，就把这里的 `10000.0f` 改大。

**注意**：`cameraPanel` **不会**自动 `updateMatrices`。它只改字段。矩阵
刷新由调用方负责，demo 里的 `CameraRig::update()` 每帧都会调一次：

```cpp
// camera_rig.cpp
if (m_mode == Mode::Orbit) {
  m_orbit.update(*m_camera, input, dt);
} else {
  m_freefly.update(*m_camera, input, dt);
}
m_camera->updateMatrices();
```

所以任何 UI 改动都会在下一帧生效。这是 REQ-018 刻意的"不把隐藏副作用
塞进 UI helper"设计。

### 改 Orbit / FreeFly 初始姿态

**代码**：`src/demos/scene_viewer/camera_rig.cpp::CameraRig::CameraRig()`：

```cpp
CameraRig::CameraRig()
    : m_orbit(LX_core::Vec3f{0.0f, 0.0f, 0.0f}, 3.0f, 0.0f, 0.0f),
      m_freefly(LX_core::Vec3f{0.0f, 0.0f, 3.0f}, 180.0f, 0.0f) {}
```

- `m_orbit` 参数：`(target, distance, yawDeg, pitchDeg)`
- `m_freefly` 参数：`(startPos, yawDeg, pitchDeg)`

注意 FreeFly 的 yaw `180°` = 让它初始朝向 `-Z`（也就是望向原点）。这是因为
它的 yaw `0°` 朝向 `+Z`。改初始姿态时留意这点。

### 切换相机模式的键

当前 `F2` 在 Orbit / FreeFly 间切换。边沿检测写死在
`CameraRig::update()`：

```cpp
const bool f2Down = input.isKeyDown(LX_core::KeyCode::F2);
if (f2Down && !m_prevF2Down) {
  switchMode();
}
m_prevF2Down = f2Down;
```

换成 `KeyCode::Tab` 就改这一处。`KeyCode` 的可选值在
`src/core/input/key_code.hpp`。

---

## 模型 / 几何

### 换模型（不加载 DamagedHelmet，加载别的 glTF）

**代码**：`src/demos/scene_viewer/main.cpp`：

```cpp
const std::filesystem::path gltfPath =
    "assets/models/damaged_helmet/DamagedHelmet.gltf";
auto helmet = demo::buildHelmetNode(gltfPath);
```

和 `cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")` 一起
换成你自己的 glTF 相对路径。

**glTF 必须**：

- `POSITION` 必需（demo 构建 mesh 时硬要求）
- `TEXCOORD_0` 推荐有；没有 `baseColorTexture` 绑定就不会触发
- `TANGENT` 可以没有（demo 用占位 `{1,0,0,1}` + `enableNormal=0` 兜底）

`buildHelmetNode` 调 `infra::GLTFLoader`，只读 `meshes[0].primitives[0]`。
多 mesh 会打 warning 并取第一个 —— 想看复杂场景，这个 glue 不够，要等
真 scene graph 导入。

### 把模型缩放 / 平移 / 旋转

**代码**：当前 demo 没在 `SceneNode` 上应用 model 矩阵，`VulkanRenderer`
在 `initScene()` 里把每个 item 的 `drawData.model` 刷成 `Mat4f::identity()`。
要应用变换有两条路：

**A. 烘到顶点里**（简单但不可动态改）
`scene_builder.cpp::buildMeshFromGltf` 改成在拼 `VertexPosNormalUvBone`
时乘一个 `Mat4f`。

**B. 每帧覆盖 `drawData`**（运行时可调）
在 `main.cpp` 的 `setUpdateHook` 里：

```cpp
if (auto pd = helmet->getPerDrawData()) {
  LX_core::PerDrawLayoutBase pc{};
  pc.model = LX_core::Mat4f::translation({0.0f, 0.5f, 0.0f})
           * LX_core::Mat4f::rotationY(clock.time() * 0.5f); // 假设有
  pd->update(pc);
}
```

注意 B 会在下一帧 `uploadData` 里生效，`initScene` 只发生一次，所以
`drawData` 不会被再覆盖。如果哪天 `initScene` 被重算（`startScene` 再
调），要重新套上这段 hook。

### 改地面大小 / 高度 / 颜色

**代码**：`src/demos/scene_viewer/scene_builder.cpp::buildGroundMesh()`：

```cpp
const float half    = 20.0f;   // 40m x 40m
const float groundY = -1.5f;   // 头盔下方 1.5m
```

颜色在 `makeGroundMaterial()`：

```cpp
mat->setVec3(StringID("baseColor"), Vec3f{0.4f, 0.4f, 0.45f});
```

`baseColor` 要匹配 shader 的 MaterialUBO 布局（定义在
`shaders/glsl/blinnphong_0.frag` 里：`vec3 baseColor; float shininess;
float specularIntensity; int enableAlbedo; int enableNormal; int padding;`）。
改完**一定**要走 `mat->syncGpuData()` —— `MaterialInstance::set*` 只
改 CPU 镜像，`syncGpuData` 才标 dirty。

---

## 帧率 / 性能 / 诊断

### 看 FPS / delta time

已经在 `Stats` 面板里：`debug_ui::renderStatsPanel(clock)` 显示：

- frame count
- dt (ms) — 当前帧 delta
- fps — 从 `clock.smoothedDeltaTime()` 推导的平滑 FPS

实现在 `src/infra/gui/debug_ui.cpp`：

```cpp
void renderStatsPanel(const LX_core::Clock& clock) {
  separatorText("Frame");
  labelInt("frame", static_cast<int>(clock.frameCount()));
  labelFloat("dt (ms)", clock.deltaTime() * 1000.0f);
  const float smoothed = clock.smoothedDeltaTime();
  const float fps = smoothed > 0.0f ? 1.0f / smoothed : 0.0f;
  labelFloat("fps", fps);
}
```

### 打开渲染 debug 日志

一堆 env 开关，都在 `src/backend/vulkan/vulkan_renderer.cpp` 被 sniffed：

```sh
export LX_RENDER_DEBUG=1            # 总开关，每帧打印一点信息
export LX_RENDER_DEBUG_CLEAR=1      # 把清屏颜色改成蓝，便于看脏帧
export LX_RENDER_DISABLE_CULL=1     # （真正进入 pipeline state 要额外改）
export LX_RENDER_DISABLE_DEPTH=1    # 同上
export LX_RENDER_FLIP_VIEWPORT_Y=1  # viewport Y 翻转（Y-up / Y-down 切换）
```

只有 `LX_RENDER_DEBUG` 和 `LX_RENDER_DEBUG_CLEAR` 和 `LX_RENDER_FLIP_VIEWPORT_Y`
会真的影响 renderer 的行为（其他是占位日志，未接 pipeline）。

### 切换 Help 面板

F1 切换。如果觉得 F1 被 SDL / ImGui 吃了，可以在
`src/demos/scene_viewer/ui_overlay.cpp::handleHotkeys` 改成别的键：

```cpp
const bool f1Down = input.isKeyDown(LX_core::KeyCode::F1);
if (f1Down && !m_prevF1Down) {
  m_helpVisible = !m_helpVisible;
}
m_prevF1Down = f1Down;
```

---

## 调完以后别忘了

1. **改了 `.material` / shader** → 重 build（CMake 自动跑 `CompileShaders`
   target，前提是你的 build system 在 watch 这些文件）。
2. **改了 `MaterialUBO` 成员** → `setVec3` / `setFloat` / `setInt` 的
   `StringID` key 必须和 shader 里的字段名一致，否则 `material_instance.cpp`
   里会 `assert(slot && "setXxx: member not found in any buffer slot")`。
3. **改了 texture 绑定点名字** → `setTexture(StringID, ...)` 的 key 要和
   shader 里 `layout(set=2, binding=N) uniform sampler2D xxx` 的 `xxx`
   一致。绑不存在的 name 会 `assert(bindingOpt && "texture binding not
   found in material-owned bindings")` —— 就是我们修 `albedoMap` 时踩的那
   个坑。
4. **光 / 相机改了但没反应** → 99% 是忘了 `setDirty()` / `updateMatrices()`
   / `syncGpuData()` 这三个之一。

---

## 常见陷阱小抄

| 症状 | 第一嫌疑 |
|------|----------|
| UBO 改了但画面没变 | `setDirty()` 没调 |
| 相机改了 position 但画面没跟随 | `camera.updateMatrices()` 没调 |
| 材质改了 baseColor 但颜色不变 | `mat->syncGpuData()` 没调 |
| `setTexture` 触发 assert | 材质变体不包含那个 binding（如 `USE_UV` 关着时没 `albedoMap`） |
| `setInt/setVec3` 触发 assert | MaterialUBO 里没这个字段，拼写错了 |
| 窗口 resize 后不渲染 | Window::getWidth/getHeight 缓存没更新（已修） |
| 面具看到里面不是外面 | 没应用 glTF 节点变换，面法线反了 —— 用 `cullMode: None` 或调换 Back/Front |

---

## 相关文件索引

- 启动装配：`src/demos/scene_viewer/main.cpp`
- 几何 / 材质 glue：`src/demos/scene_viewer/scene_builder.cpp`
- 相机 rig：`src/demos/scene_viewer/camera_rig.cpp`
- UI 面板：`src/demos/scene_viewer/ui_overlay.cpp`
- ImGui 上下文 / 初始化：`src/infra/gui/imgui_gui.cpp`
- 面板 helper 实现：`src/infra/gui/debug_ui.cpp`
- Blinn-Phong shader：`shaders/glsl/blinnphong_0.vert` / `.frag`
- 材质资产：`materials/blinnphong_*.material`
- Camera 类：`src/core/scene/camera.hpp`
- DirectionalLight：`src/core/scene/light.hpp`
- Clock：`src/core/time/clock.hpp`
- 输入枚举：`src/core/input/key_code.hpp`
