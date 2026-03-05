小型渲染器
--- 
# 架构设计
```
 external/ 依赖的第三方库
 src/ 引擎核心源码
 ├── core/              引擎核心对象 + 基础数学工具。不依赖系统中其他任何模块。
 │    ├── math/
 │    ├── timer/
 │    ├── uuid/
 │    ├── scene/         Scene、Entity、Components、Material
 │    ├── resources/     Mesh、Texture、Material 描述数据
 │
 ├── graphics/          
 │    ├── vulkan/ 封装 Vulkan，实现渲染功能。可以依赖 core, infra 模块。
 │        ├── device/
 │        ├── swapchain/
 │        ├── pipeline/
 │        ├── renderpass/
 │        ├── shader/
 │
 ├── infra/             第三方库的封装和适配，依赖core
 │    ├── window/       SDL/GLFW
 │    ├── gui/          ImGui
 │    ├── filesystem/
 │    ├── logger/
 │
 ├── application/       程序入口，初始化 engine + main loop 。依赖 core、graphics、infra 模块。也对依赖关系进行编排，完成DI（依赖注入）。
 │    ├── game_app.cpp
 └── main.cpp
```
