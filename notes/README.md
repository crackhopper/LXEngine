# 项目速览

> 一个从 Vulkan 渲染器出发，逐步走向 **AI Native 小型游戏引擎** 的 C++20 工程。这里的首页只保留最重要的描述，细节拆到各专题文档中。

## 项目定位

`LXEngine` 当前的起点仍然是一个强调渲染基础能力的工程：Vulkan backend、材质系统、shader 编译与反射、scene / frame graph、pipeline identity，这些是引擎继续向上生长的底座。

这个项目的目标已经不再是单纯做一个“教学型 renderer”，而是以这些底层能力为起点，构造一个 **AI Native 的小型游戏引擎**：

- AI 不是外挂式工具，而是引擎的一等使用者。
- 引擎不仅要能渲染，还要能被 agent 读懂、调用、扩展和协作。
- 文本内省、命令接口、MCP、Web 编辑器、脚本与资产管线，都会围绕这个方向继续演进。

关于这条路线的展开思考，可以结合两部分内容一起看：

- [Roadmap · 走向 AI-Native 小型游戏引擎](roadmaps/README.md)
- 外部文章：[关于 AI Native 游戏引擎的思考](https://crackhopper.github.io/2026/04/02/%E5%85%B3%E4%BA%8Eai-native%E6%B8%B8%E6%88%8F%E5%BC%95%E6%93%8E%E7%9A%84%E6%80%9D%E8%80%83/)

## 当前基座

当前工程已经具备一组足够坚实的渲染器基础设施：

- `C++20 + CMake` 的跨平台工程组织
- `core / infra / backend` 三层分离
- Vulkan 渲染后端
- `shaderc + SPIRV-Cross` 驱动的 GLSL 编译与反射
- `MaterialInstance / PipelineKey / FrameGraph / RenderQueue` 等渲染主干能力
- SDL3 / GLFW 窗口层、OBJ / GLTF / texture loader、ImGui 集成

这些内容决定了 `LXEngine` 的第一性原点：先把“渲染器应该如何干净地组织起来”做扎实，再向 gameplay、编辑器、agent runtime 和 AI 资产生成扩展。

## 阅读入口

- [项目目录结构](project-layout.md)：仓库分层、主目录职责、事实来源
- [术语概念](glossary.md)：项目自造词与关键对象的一句话定义
- [架构总览](architecture.md)：三层结构、资源生命周期、场景启动与每帧工作流
- [引擎循环](引擎循环.md)：面向入门者的运行时总览，解释 `EngineLoop` 的设计考虑、阶段边界和使用形状
- [EngineLoop 子系统](subsystems/engine-loop.md)：运行时编排层，说明 `startScene/tickFrame/run` 的职责边界
- [子系统文档](subsystems/index.md)：逐个模块看 shader、material、frame graph、scene、backend
- [Roadmap](roadmaps/README.md)：从当前基座走向 AI Native 小型游戏引擎的阶段规划
- [文档工具链](tools/index.md)：`notes` 站点如何生成、如何索引、如何本地预览

## 一句话总结

`LXEngine` 的核心方向是：**以一个干净、可验证、可演进的渲染器为起点，长成一个对人类开发者和 AI agent 都友好的小型游戏引擎。**
