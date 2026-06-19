# Tutorial：从 Editor 到 Offline Ray Tracer

这组教程解决一个连续问题：我们先把 LXEngine 跑起来，在 `lxe_editor` 里搭场景、改材质和光源，再进入 shadow、PBR + IBL 与 offline ray tracer。阅读顺序按依赖和难度排列：先能构建和保存场景，再理解材质/光照和多 pass，最后读 headless offline FrameGraph 与扩展点。

## 学习线之间的依赖

| 顺序 | 系列 | 学习目标 |
|---|---|---|
| 1 | [启动项目](start-project/index.md) | 把工程构建起来，打开 editor，加载和保存场景 |
| 2 | [自定义材质](custom-material/index.md) | 理解材质系统，并写一个 Gooch shader |
| 3 | [自定义灯光](custom-light/index.md) | 理解三类内置 light、`SceneLightsUBO` 和当前 shader 消费边界 |
| 4 | [Shadow 阶段](shadow-era/index.md) | 打开 shadow / CSM 场景，理解多 pass 写读关系 |
| 5 | [PBR + IBL](pbr-ibl/index.md) | 打开 Damaged Helmet + neutral IBL 场景，理解 scene-level IBL resources |
| 6 | [Offline Renderer](offline-renderer/index.md) | 用同一份 scene 跑 headless offline ray tracer，写出 EXR/PNG，并理解 path tracing 扩展点 |
| 7 | [扩展场景节点](extend-scene-node/index.md) | 理解一种新节点如何兼容 editor 全流程 |
| 8 | [扩展编辑器](extend-editor/index.md) | 理解 toolbar 与 command bus 如何配合 |

前六条学习线偏当前实践：构建、启动、保存场景、写材质、调灯光、观察 shadow/CSM、验证 PBR/IBL、运行离线输出。最后两条学习线讲当前代码里的 command-first 与 scene-node 手工扩展触点，不再把旧的 registry 占位需求当成教程目标。

## 当前教程覆盖范围

| 路径 | 当前教程怎么讲 |
|---|---|
| 光源 | 当前 Directional / Point / Spot 的 scene、runtime、debug 和 GPU 数据链路；同时说明 realtime/offline shader 的真实消费范围 |
| Editor 扩展 | 当前新增 command、toolbar 按钮、completion、API snapshot 要同步哪些点 |
| 场景节点扩展 | 当前新增一种节点语义需要兼容 document、runtime、Inspector、command、debug draw、picking、save/load |
| 未来能力 | 不在教程主线里预告为工作流；需要重新进入 active requirement 或设计 spec 后再写教程 |

## 按目标选择阅读顺序

| 目标 | 建议路径 |
|---|---|
| 只想先跑起来 | 启动项目 01 到 03 |
| 想做一个能看见的材质实验 | 启动项目全部 + 自定义材质全部 |
| 想理解 light 和 shader 的连接 | 自定义材质 01/02 + 自定义灯光 01/02/05 |
| 想理解 0.2.0-pre 的 shadow 主线 | 启动项目全部 + Shadow 阶段全部 |
| 想理解 PBR + IBL 的当前闭环 | 启动项目全部 + PBR + IBL 全部 |
| 想跑离线渲染或准备写 path tracing | 启动项目 01 + PBR + IBL 01 + Offline Renderer 全部 |
| 想新增一种场景对象 | 启动项目 03/04 + 扩展场景节点 |
| 想改 editor 操作入口 | 启动项目 04 + 扩展编辑器 |

Assets Downloader 暂时不放在教程主线。它是开发中的相关工具，见 [相关工具 / Assets Downloader](../tools/assets-downloader.md)。

我们不要求一开始理解所有底层 Vulkan 细节。先把 editor 或 offline ray tracer 的一条链路跑通，再回头读 [场景系统](../scene-system/index.md) 和 [源码分析](../source_analysis/index.md)，会更稳。
