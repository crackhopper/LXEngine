# Vulkan Backend

> Vulkan backend 是 core 抽象到 Vulkan API 的落地点。它不决定“什么时候开始一帧”，也不负责业务层 update hook；这些编排职责已经上移到 `EngineLoop`。backend 负责把 `FrameGraph` 和 `RenderingItem` 真实提交到 GPU。
>
> 权威 spec: `openspec/specs/renderer-backend-vulkan/spec.md`

## 现在怎么读

这页是当前 Vulkan backend 的维护入口。更细的源码阅读从下方“从哪里进入源码”开始；文件级解释优先看 `notes/source_analysis/` 中已经生成的页面。

## 一页版总结

- `VulkanRenderer` 负责 orchestration：初始化、`initScene()`、`uploadData()`、`draw()`
- `VulkanDevice` 负责 instance/device/queue/surface format/depth format
- `VulkanSwapchain` 负责 swapchain image、depth、framebuffer、同步对象
- `VulkanResourceManager` 负责 CPU 资源镜像与 pipeline cache
- `VulkanCommandBuffer` 在 draw 阶段汇合 pipeline、descriptor、vertex/index buffer 和 push constants
- `VulkanRendererImpl` 现在只是 `VulkanRenderer` 的私有实现对象，renderer 继承边界由 `VulkanRenderer` 对外承担

## 当前实现最重要的约束

- 所有可能触发 Vulkan loader 初始化的可执行程序，都必须在 `main()` 一开始调用 `LX_core::expSetEnvVK()`
- 这不是“可选清理项”，而是为了抑制 implicit validation layer 自动加载时额外产生的 `.log` 文件；调用必须早于 window / renderer / Vulkan instance 初始化
- Linux 下的窗口化 Vulkan/SDL 测试还要求 `libSDL3.so.0` 可用，并且必须有视频设备来源：真实桌面会话，或者 `Xvfb`
- 在 headless Linux shell 里，优先用 `xvfb-run -a ./src/test/<test-binary>`；这就足够让 SDL 拿到 video device，不需要额外装完整桌面
- 如果没跑在桌面或 `Xvfb` 下，这类测试通常会以 `No available video device` 明确 skip；先排环境，再排 renderer 逻辑
- 物理设备选择现在按“先判定功能是否满足，再按设备类型偏好排序”处理；独显优先，但集显/虚拟 GPU 只要满足队列、扩展和 surface 要求也允许启动
- descriptor 路由按 binding name，不按硬编码 slot 枚举
- scene-level UBO 已经在 queue 构建阶段合并好，backend 按 `RenderingItem` 中的资源录制 descriptor
- `VulkanResourceManager` 不直接持有旧式 pipeline map，而是委托给 `PipelineCache`
- `VulkanResourceManager` 现在按 `IGpuResource::getBackendCacheIdentity()` 做 cache key，资源身份来自显式 backend cache identity
- GPU 资源缓存带短暂闲置宽限期：资源漏同步一帧不会立刻销毁重建，但长期不用仍会被 `collectGarbage()` 回收
- `FrameGraph` 当前执行 4 个 `Pass_Shadow` cascade，再执行 `Pass_Forward` 和需要的 debug / overlay 路径
- `kMaxFramesInFlight` 在 `VulkanRenderer` 内部只有一个定义，初始化路径和 draw 路径共用同一来源
- Vulkan viewport 现在固定使用单一约定：`x=0`、`y=h`、`width=w`、`height=-h`，runtime 使用这套固定配置

## 屏幕坐标约定

当前 backend 统一采用一套固定 viewport Y 约定：

| 层级 | 当前约定 | 代码入口 |
|---|---|---|
| editor / 屏幕像素 | 左上角原点，`x` 向右，`y` 向下 | `SceneInteractionController` / `ViewportOverlay` |
| NDC | 围绕屏幕中心的 `[-1, 1]` 归一化坐标；项目采用 OpenGL 风格屏幕语义：上方像素对应正 `y`，下方像素对应负 `y`；`z` 落在 Vulkan 风格 `0..1` 深度链路 | `CameraComponent::pickRay()` / `getProjMatrix()` |
| Vulkan viewport | `VkViewport{0, h, width, -height, 0, 1}`，把 clip/NDC 的 `+Y` 映射到屏幕上方 | `details/commands/command_buffer.cpp` |

这样做的结果是：CPU 侧的 pick / project 公式继续使用“上正下负”的 OpenGL 风格 NDC，GPU 侧只在 viewport 这一个固定位置完成落屏 Y 翻转；运行时环境变量不会插入第二次镜像。

## 从哪里进入源码

- 顶层：`src/backend/vulkan/vulkan_renderer.cpp`
- 资源：`src/backend/vulkan/details/resource_manager.cpp`
- pipeline：`src/backend/vulkan/details/pipelines/`
- descriptor：`src/backend/vulkan/details/descriptors/`
- draw 命令：`src/backend/vulkan/details/commands/`
