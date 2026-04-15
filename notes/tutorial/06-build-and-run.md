# 06 · Build & Run

> 把前 5 章的产出接入当前 CMake 结构，运行起来，遇到问题按清单排查。这里所有命令和路径都以仓库当前的 `CMakeLists.txt` 为准。

## 文件清单（回顾）

前 5 章产出的文件：

| 文件 | 章节 |
|------|------|
| `shaders/glsl/pbr.vert` | 02 |
| `shaders/glsl/pbr.frag` | 02 |
| `src/infra/material_loader/pbr_material_loader.hpp` | 03 |
| `src/infra/material_loader/pbr_material_loader.cpp` | 03 |
| `src/test/test_pbr_cube.cpp` | 05 |

## CMake 改动

### 1. Shaders

`shaders/CMakeLists.txt` 里 `file(GLOB ...)` 会自动抓到新的 `.vert` / `.frag`。当前仓库已经有 `pbr.vert` / `pbr.frag`，所以如果你只是改内容，通常直接 build 就行；如果你新增了别的 shader 文件名，还是要重新 configure 一次。

```bash
cd build
cmake ..                     # 刷新 shader glob
ninja CompileShaders         # 或直接 ninja test_pbr_cube 会级联
```

### 2. Infra loader

`src/infra/CMakeLists.txt` 现在维护的是 `INFRA_SOURCES` 列表。找到 Blinn-Phong loader 的那一行：

```bash
rg -n "blinn_phong_material_loader" src/infra/CMakeLists.txt
```

在下面追加：

```cmake
material_loader/pbr_material_loader.cpp
```

### 3. Test app target

`src/test/CMakeLists.txt` 里有一个 `TEST_APP_EXE_LIST`，当前只有 `test_render_triangle`。把 PBR 示例也加进去：

```cmake
set(TEST_APP_EXE_LIST
  test_render_triangle
  test_pbr_cube            # ← 新增
)
```

`foreach` 循环会为它生成 executable、链接 core/infra/graphics、并加入 `BuildTest` 目标。

## 构建

```bash
cd build
cmake .. -G Ninja            # 第一次 / 加了新文件后都要重跑
ninja test_pbr_cube          # 会级联 CompileShaders → infra → test_pbr_cube
```

## 运行

推荐入口形状是 `EngineLoop`：先 `initialize(window, renderer)`，再 `startScene(scene)`，最后 `setUpdateHook(...)` 后调用 `run()`。这也是当前仓库里 `test_render_triangle.cpp` 已经采用的模式。


```bash
./test_pbr_cube
```

带调试日志：

```bash
LX_RENDER_DEBUG=1 ./test_pbr_cube
```

可用调试环境变量（见 `src/backend/vulkan/vulkan_renderer.cpp`）：

| 变量 | 效果 |
|------|------|
| `LX_RENDER_DEBUG=1` | 打印 extent / initScene 统计 / heartbeat |
| `LX_RENDER_DEBUG_CLEAR=1` | 清屏色改成蓝色，用来确认 renderpass 有没有生效 |
| `LX_RENDER_DISABLE_CULL=1` | 关掉 back-face culling，用来排查 winding 问题 |
| `LX_RENDER_DISABLE_DEPTH=1` | 关掉深度测试，用来排查深度 format 问题 |
| `LX_RENDER_FLIP_VIEWPORT_Y=1` | 翻转 viewport Y，用来排查上下颠倒 |

---

## 看到的应该是什么

- 深红色立方体在窗口中央
- 缓慢绕垂直轴旋转
- 侧面和顶面亮度不同（单侧高光 + lambert 漫反射）
- 背对光的那一面不是纯黑，而是 $ambient = 0.03 \cdot baseColor \cdot ao$ 的暗红

调 `roughness`：从 `0.1` 到 `0.9` 高光会从尖锐的亮斑扩散成柔和的高光盘。

调 `metallic`：`0.0` 是塑料感；`1.0` 且 `baseColor` 放深色时变成一块深色金属——因为金属没有漫反射，整块立方体的明度只取决于 Fresnel + 高光。

---

## 排错清单

### 窗口出来了，但画面全是清屏色

- 检查 shader SPIR-V 是不是真的编出来了：`ls build/shaders/glsl/pbr.*.spv`
- 重新跑一次 `cmake ..` 让 shader glob 刷新
- `LX_RENDER_DEBUG=1` 看 initScene 里 `totalItems` 是否为 1、`preloadedPipelines` 是否 ≥ 1。0 说明 scene 里没有 renderable 或者 pipeline key 构造失败

### 看到一个纯色立方体，但没有光照差异

- shader 里用的是不是 `vNormal` 而不是 `inNormal`？
- C++ 端每面法线是否正确填入？打个 print 把 `cubeVertices[0].normal.x` 之类打一遍
- 光方向不要全零：`dir = (0,0,0,0)` 会让 `normalize(-light.dir.xyz)` 出 NaN

### 立方体整个消失（只剩清屏色）

1. **Winding 反了** → `LX_RENDER_DISABLE_CULL=1` 再跑，如果出现就是 winding 问题，把 04 章索引模板调换两个三角形顺序
2. **相机在立方体里面** → 相机 `position = (0,0,2.5)`，确认不要设成 0
3. **near/far plane 太小** → 默认 `nearPlane=0.1, farPlane=1000`，应该没问题。若改过检查一下

### `MaterialInstance` 构造时失败，找不到 `MaterialUBO`

- shader 里的 `uniform` 块名一定要是 `MaterialUBO`（不是 `PbrParams` 也不是 `Material`）
- 用 `glslangValidator -V` 或打开 SPIRV-Cross 看反射结果，确认 binding 里确实有 `"MaterialUBO"` 这个名字

### `setVec4("baseColorFactor", ...)` / `setFloat("metallicFactor", ...)` assert fail

- 反射里找不到 `baseColorFactor` 或 `metallicFactor`：shader 里的 member 名必须和 C++ setter 使用的名字一致
- 类型不匹配：shader 写 `vec4 baseColorFactor` 但 C++ 仍调用 `setVec3`，类型校验会 fail。要么 shader 改回 `vec3` 方案，要么 C++ 端改成 `setVec4`

### PipelineCache miss 警告

- 通常说明 `RenderingItem.pipelineKey` 运行期计算和 initScene preload 时不一致
- 多半是中间改过 material 的 render state / shader set 但没重新 `buildCache()`
- 改 material 参数（UBO 内容）**不会**影响 PipelineKey，只改 std140 字节 buffer

---

## 测试建议

本教程没写单元测试。落到项目里如果想加，可以参考：

- `src/test/integration/test_material_instance.cpp` — 构造 + UBO 写入非 GPU 测试
- `src/test/integration/test_frame_graph.cpp` — FrameGraph 构建 + RenderQueue::buildFromScene 过滤

建议的 PBR 专属测试：
- 构造 `loadPbrMaterial()` 不抛异常
- `setVec4("baseColorFactor", ...)` 后读回字节 buffer，确认反射 member 偏移正确
- `setFloat("metallicFactor", 0.5f)` / `setFloat("roughnessFactor", 0.5f)` 后读回字节 buffer，确认没有写错偏移
- `RenderQueue::buildFromScene` 上 PBR renderable + Pass_Forward 能产出 1 个 item，`item.pipelineKey.id.id != 0`

这些都可以抄 `test_material_instance.cpp` 改几行完成，不需要 Vulkan device。

---

## 教程结束

到此为止你有：
- 一个独立的 PBR shader 对
- 一个 material loader，严格遵循项目的反射驱动范式
- 一个 main，展示了如何在已有 Scene / FrameGraph 基础上放一个旋转的物体

要继续把它"真的做成一个引擎"，合理的下一步：
- **贴图**：把 albedo / metallic-roughness / normal 贴图接上 — 走 `src/infra/texture_loader/` + `material->setTexture()`
- **多物体**：`scene->addRenderable(...)` + 不同的 `model` 矩阵
- **环境光**：用 cubemap + IBL 替换掉 `vec3(0.03)` 的环境项
- **Shadow map**：加一个 `Pass_Shadow` 到 `FrameGraph`，让 `DirectionalLight` 的 `passMask` 把自己加进来
- **相机控制**：接上 `window->onMouseMove` / `onKey` 做 WASD + 鼠标 look

每一个方向都可以独立展开成一个类似本教程长度的系列。

← [00-overview.md](00-overview.md) | 首页
