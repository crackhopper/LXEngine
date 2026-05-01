# REQ-039: DebugDraw 子系统 — 一行调用画世界空间线 / 球 / 视锥 / 锥 / 箭头 / 坐标轴

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 5 步。在 roadmap 中以"REQ-150 DebugDraw 子系统"前向声明。

## 背景

LX 当前**完全没有**高层"画一根世界空间线"的 API。底层 `PrimitiveTopology::LineList`（`src/core/scene/index_buffer.hpp:18-25`）+ backend 转换（`pipeline.cpp::topologyToVk`）已经存在，但要画一根线得：

1. 手动构造一个 `SceneNode` + LineList topology mesh
2. 写一个 line-compatible material
3. 每帧自己更新 vertex buffer
4. 整套接进 PipelineCache

这套门槛让"加一行调用画一根线"在业务代码里完全不可用，严重拖累 [REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) 的 frustum / 光源箭头 / 选中线框 / picking ray 等可视化需求 —— 而且未来 RTR 章节实验里的 BVH 调试线、shadow cascade 边界、AS 命中点等也依赖这个能力。

用户在 Phase 1.5 设计讨论中明确："**易用性硬指标：用户在任意业务代码中一行调用即可画一根世界空间线。**"

## 目标

1. 提供静态 / 单例式公共 API：`DebugDraw::drawLine(...)` / `wireSphere(...)` / `frustum(...)` / `cone(...)` / `arrow(...)` / `axis(...)`
2. 累积每帧 line 顶点 → 单一 LineList pipeline → FrameGraph 的一个 debug overlay pass 一次绘制
3. 输出节点带 `Layer_EditorOverlay` mask；编辑器相机包含此 bit、游戏相机不含 → 自动隔离
4. 单帧调用上限默认 100k 条线；超出 LOG_WARN 并裁剪
5. 调用线程安全：UI 线程与 render 线程都能调用（v1 用单线程缓冲 + 显式 frame begin/end，简单实现）

## 需求

### R1: 公共 API（静态命名空间）

`src/core/debug_draw/debug_draw.hpp`（新）：

```cpp
namespace DebugDraw {

// 基本图元
void drawLine(Vec3f a, Vec3f b, Color color = Color::white());
void drawTriangle(Vec3f a, Vec3f b, Vec3f c, Color color);  // 仅画 3 条边

// 复合图元
void wireSphere(Vec3f center, float radius, Color color, int segments = 24);
void wireCircle(Vec3f center, Vec3f normal, float radius, Color color, int segments = 24);
void wireBox(const AABB &aabb, Color color);
void wireBox(Vec3f center, Vec3f extent, Quatf rotation, Color color);
void cone(Vec3f apex, Vec3f direction, float length, float halfAngleRad, Color color, int segments = 16);
void arrow(Vec3f from, Vec3f to, Color color, float headSize = 0.1f);
void axis(const Mat4f &transform, float length = 1.0f);   // 三轴 RGB

// 相机视锥（perspective + orthographic 都支持）
void frustum(const Mat4f &viewProj, Color color);

}
```

- `Color` 用仓库现有 `Vec4f` alias 或新增 `Color` 值类型（按照 `src/core/math/` 现状择一，不强制新增）
- `axis(transform)` 以 transform 的 origin 起，三轴方向各画一根带颜色的箭头（X 红 / Y 绿 / Z 蓝）
- 所有调用都立即返回，不阻塞，不分配大块内存（push 进内部 vertex buffer）

### R2: `Color::white() / red() / green() / blue() / yellow()` 等常量

提供基础颜色常量，避免业务代码每次写 `Vec4f{1, 1, 1, 1}`。

### R3: 单帧 vertex buffer + LineList pipeline

`src/core/debug_draw/debug_draw.cpp`：

- 内部维护 `std::vector<DebugLineVertex> m_vertices`（per-frame）
  ```cpp
  struct DebugLineVertex { Vec3f position; Vec4f color; };
  ```
- `DebugDraw::beginFrame()` / `DebugDraw::endFrame()` 接口（在 `EngineLoop` 里调用）
- `endFrame` 内部把 m_vertices 上传到一个 vertex buffer（`IGpuBuffer`），构造一个临时 `SceneNode`（可见 mask = `Layer_EditorOverlay`）注册到 scene
- 下一帧 `beginFrame` 清空 m_vertices

### R4: `Layer_EditorOverlay` 常量

`src/core/scene/visibility_mask.hpp`（如已存在则在其中追加；否则新建）：

```cpp
constexpr u32 Layer_All = 0xffffffffu;
constexpr u32 Layer_Default = 1u << 0;
constexpr u32 Layer_EditorOverlay = 1u << 31;     // 高位预留给编辑器
```

- 编辑器相机 cullingMask 默认包含 `Layer_EditorOverlay`
- 游戏相机 cullingMask 默认不含 `Layer_EditorOverlay`
- DebugDraw 输出节点的 visibilityLayerMask = `Layer_EditorOverlay`（默认；R5 提供覆盖）

### R5: `DebugDraw::scope` — 临时切层

```cpp
struct LayerScope {
  LayerScope(u32 mask);           // 进入时切当前 mask
  ~LayerScope();                  // 退出时恢复
};
```

业务代码：

```cpp
{
  DebugDraw::LayerScope s{Layer_All};   // 临时让游戏相机也看见
  DebugDraw::wireSphere(center, r, Color::red());
}
```

### R6: 单一 pipeline + FrameGraph 接入

- 注册一个 `Pass_DebugOverlay` StringID，与现有 `Pass_Forward` / `Pass_Deferred` / `Pass_Shadow` 同构
- 该 pass 在 FrameGraph 中位于主 forward pass 之后、UI（ImGui）overlay 之前
- pipeline key：line topology + 一个简单 unlit color shader（`assets/shaders/glsl/debug_line.vert/frag`）
- 该 pipeline 在 PipelineCache 启动期 preload，runtime 不再编译

### R7: shader 协议

`assets/shaders/glsl/debug_line.vert`：

```glsl
#version 450
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;
layout(set = 0, binding = 0) uniform CameraUBO { mat4 viewProj; } u_cam;
layout(location = 0) out vec4 v_color;
void main() {
    v_color = a_color;
    gl_Position = u_cam.viewProj * vec4(a_pos, 1.0);
}
```

`debug_line.frag`：

```glsl
#version 450
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 o_color;
void main() { o_color = v_color; }
```

- depth test 启用、depth write 关闭（debug 线不应遮挡也不应被遮挡）—— 文档明确（防止 dirty depth state）
- alpha blend 开启（线尾 fade 后续可加，v1 不做）

### R8: 单帧上限 + 警告

- 默认 `kMaxLinesPerFrame = 100000`
- 超出时 LOG_WARN 一次（per frame，不要刷屏）
- 超出时丢弃最新 push 的线，不阻断帧

## 测试

- `DebugDraw::drawLine(...)` 三次后，`endFrame` 后 vertex buffer 含 6 个 vertices
- `DebugDraw::wireSphere` 用 24 段时画 24×3 段（XY / YZ / ZX 三圈）
- `DebugDraw::frustum(viewProj)` 输出 12 段线（near / far 矩形 + 4 条连线）
- `Layer_EditorOverlay` 与默认游戏相机的 cullingMask 求交为 0 → 游戏相机不渲染
- 编辑器相机 cullingMask 包含 `Layer_EditorOverlay` → 渲染
- 一个完整渲染帧后 `m_vertices.empty()`（beginFrame 清空生效）
- 超过 100k 条线时记录 LOG_WARN

## 修改范围

- `src/core/debug_draw/debug_draw.hpp` / `.cpp`（新）
- `src/core/scene/visibility_mask.hpp`（新或扩展）
- `src/core/frame_graph/pass.hpp`（加 `Pass_DebugOverlay`）
- `assets/shaders/glsl/debug_line.{vert,frag}`（新）
- `src/core/gpu/engine_loop.cpp`（每帧 begin/end）
- `src/backend/vulkan/`（如 line topology pipeline 需要特殊处理，做最小适配）
- `src/test/integration/test_debug_draw.cpp`（新）

## 边界与约束

- v1 **不**做线宽（line width 在 Vulkan 是 device feature，需要 `wideLines` extension；先用 1 像素）
- v1 **不**做线条 anti-aliasing
- v1 **不**做 screen-space 字体（"draw text at world position"）
- v1 **不**做 persistent draw（每个 draw 命令仅活在当前帧）；persistent 留 v2
- v1 **不**做 mesh-as-debug（"画整个 mesh 的线框"）；用户可手动遍历 triangle 调 `drawTriangle`
- 单一 pipeline，不支持 user 自定义 shader

### REQ-042 兼容预留

`Pass_DebugOverlay` 的 `FramePass` 在 R6 实施时按当前 RenderTarget API 写（沿用占位 `RenderTarget` 三字段）。[REQ-042 R1-R8](042-render-target-desc-and-target.md) 后置到 Phase 1.5 完工后、Phase 1 REQ-103 之前实施时，会把 `FramePass` 的 target 字段从 `RenderTarget` 拆为 `RenderTargetDesc + RenderTarget`；届时本 REQ 的 `Pass_DebugOverlay` 注册路径同步更新（仅改字段类型，不改 pass 语义）。DebugDraw 的 vertex / shader / pipeline 协议（R3 / R6 / R7）与 attachment 形状解耦，REQ-042 升级对 DebugDraw 内部数据流透明。

## 依赖

- 现有 `PrimitiveTopology::LineList`（`src/core/scene/index_buffer.hpp:18-25`）
- 现有 PipelineCache + ShaderCompiler
- 现有 FrameGraph
- [REQ-038](038-ray-aabb-picking-min.md) `AABB` 类型 — `wireBox(AABB)` 重载用

## 后续工作

- [REQ-041 ImGui Editor MVP](041-imgui-editor-mvp.md) — frustum / directional light arrow / 选中节点 wire box / picking ray 全部用 DebugDraw
- 未来 [REQ-109 PointLight + SpotLight](../roadmaps/main-roadmap/phase-1-rendering-depth.md#req-109--pointlight--spotlight--统一多光源合同) 落地后，point light 衰减球用 `wireSphere`、spot light 锥用 `cone`，一行调用接通
- BVH / shadow cascade 边界 / AS 命中点等未来调试可视化都消费同一套 API

## 实施状态

待实施。Phase 1.5 第 5 步。在 [REQ-038 AABB](038-ray-aabb-picking-min.md) 落地后开工（`wireBox(AABB)` 重载需要它）。
