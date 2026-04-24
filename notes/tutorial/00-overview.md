# Tutorial — 从零到 PBR 旋转立方体

> 本系列把当前代码库当作一个**小型渲染引擎**来使用，目标是在现有 `EngineLoop + Scene + MaterialTemplate + VulkanRenderer` 封装之上接入一条 PBR 路径。文中所有“当前已有”与“需要新增”的边界都以仓库现状为准。

## 这个项目算引擎吗？

短答：**是一个刚刚够格的、教学型的小型渲染引擎**，不是游戏引擎。从"引擎"这个词的标准含义看，它具备的能力：

- **资源抽象层**：`IGpuResource` + dirty 同步通道（`notes/architecture.md` 场景启动与每帧工作流）
- **材质系统**：反射驱动的 `MaterialTemplate` / `MaterialInstance`，std140 自动打包（`notes/subsystems/material-system.md`）
- **Shader 系统**：运行期 GLSL → SPIR-V → 反射 → `CompiledShader`（`notes/subsystems/shader-system.md`）
- **管线身份**：`PipelineKey` + `PipelineBuildDesc` + `PipelineCache` 预构建（`notes/subsystems/pipeline-identity.md` / `pipeline-cache.md`）
- **Scene / FrameGraph**：多相机 + 多光源 + 多 pass + 按 target/pass 过滤 scene-level 资源（`notes/subsystems/scene.md` / `frame-graph.md`）
- **Vulkan backend**：device / swapchain / resource manager / command buffer / descriptor manager 一套薄壳（`notes/subsystems/vulkan-backend.md`）

它**还不具备**：场景图 transform 层级、动画播放器、物理系统、资产管线工具、运行期编辑器、序列化 / 反序列化、任务系统、音频、输入层（只有窗口事件回调）、光照烘焙、后期处理链路、可切换 backend。

**定位建议**：把它当作“一个对概念建模相对干净的 Vulkan 教学引擎”。加新特性时可以直接复用已有的资源 / 材质 / pipeline 抽象；做一个完整游戏则需要自己补上述缺口。本教程要做的，就是按当前封装方式把一条 PBR 路径接进去。

---

## 教程要做什么

**最终目标**：一个窗口里，一个带金属度和粗糙度参数的立方体，在一盏方向光下自转，画面没有纹理依赖、能量守恒、支持 tone mapping + gamma。

**当前代码状态**：
- 引擎封装已经有：`EngineLoop`、`Scene`、`SceneNode`、`MaterialTemplate/MaterialInstance`、`VulkanRenderer`
- 已有可运行入口：`src/test/test_render_triangle.cpp`
- 已有材质 loader：`src/infra/material_loader/blinn_phong_material_loader.cpp`
- 已有 PBR shader 原型：`shaders/glsl/pbr.vert`、`shaders/glsl/pbr.frag`
- 还没有完整落地的 PBR loader / PBR 示例程序 / `test_pbr_cube`

**不会用到**的部分（为了聚焦 PBR 本身）：
- 纹理采样（albedo / normal / metallic-roughness map）
- 骨骼动画
- 多 pass（只 `Pass_Forward`）
- 多相机 / 多目标渲染

**会用到**的子系统：

```
 Scene (1 camera + 1 directional light + 1 cube renderable)
    │
    ▼
 EngineLoop::startScene
    │
    ▼
 FrameGraph (Pass_Forward → swapchain)
    │
    ▼
 RenderQueue::buildFromScene → RenderingItem (with PipelineKey)
    │
    ▼
 EngineLoop::tickFrame
    │
    ▼
 update hook -> upload dirty -> VulkanRenderer::draw
```

---

## 目录

| # | 文件 | 内容 |
|---|------|------|
| 01 | [pbr-theory.md](01-pbr-theory.md) | PBR 基础：渲染方程 → Cook–Torrance → GGX / Smith / Schlick |
| 02 | [pbr-shader.md](02-pbr-shader.md) | 对齐当前仓库里的 `pbr.vert` / `pbr.frag`，补齐它与现有引擎封装的接口关系 |
| 03 | [material-loader.md](03-material-loader.md) | 基于现有 `blinn_phong_material_loader.cpp`，说明 PBR loader 应该怎样落地 |
| 04 | [cube-geometry.md](04-cube-geometry.md) | 构造 24 顶点立方体，每面独立法线 |
| 05 | [app-main.md](05-app-main.md) | 基于当前 `EngineLoop` 工作流，把 Scene / Camera / Light / Renderable 串成一个入口 |
| 06 | [build-and-run.md](06-build-and-run.md) | 基于现有 CMake 结构补齐 PBR 接线，并给出构建 / 运行 / 排错清单 |

---

## 预备知识

- 读过 `notes/architecture.md` 的"场景启动与每帧工作流"一节
- 对 `EngineLoop` 的职责边界有基本印象（`notes/subsystems/engine-loop.md`）
- 了解 Vulkan 的 descriptor set / pipeline 基本概念（不必会写，能看懂字段名即可）
- 看过 `src/test/test_render_triangle.cpp`，因为它就是当前代码里最接近“最小可运行渲染入口”的真实样板

## 做完后你会理解

- 为什么 PBR 的 "metallic-roughness workflow" 只需要两个标量就能覆盖大部分真实材质
- 为什么 `MaterialUBO` 这个名字在本项目里是硬约定
- 为什么一个 shader 或变体集合的变化可能触发 `PipelineKey` 改变并强制重建 pipeline
- 为什么 `std140` 里 `vec3` 后面跟 `float` 是**故意**紧挨着放的
- 一帧数据怎么从 C++ 对象走到 GPU 的 uniform buffer 和 push constant

准备好了就翻到 [01-pbr-theory.md](01-pbr-theory.md)。
