# Tutorial：从跑起来到离线实验室

这组教程解决一个连续问题：我们先把 LXEngine 跑起来，再学会改变场景里的材质和光源，然后进入 shadow、PBR + IBL 与 offline renderer 这些更完整的渲染链路，最后再学习怎样扩展场景节点和 editor 行为。阅读顺序按依赖和难度排列：先能构建和保存场景，再理解材质/光照，再看多 pass 和 HDR，最后读 headless compute 与扩展点。

## 学习线之间的依赖

| 顺序 | 系列 | 学习目标 |
|---|---|---|
| 1 | [启动项目](start-project/index.md) | 把工程构建起来，打开 editor，加载和保存场景 |
| 2 | [自定义材质](custom-material/index.md) | 理解材质系统，并写一个 Gooch shader |
| 3 | [自定义灯光](custom-light/index.md) | 理解当前 light 底座和未来 custom light 工作流 |
| 4 | [Shadow 阶段](shadow-era/index.md) | 打开 shadow / CSM 场景，理解多 pass 写读关系 |
| 5 | [PBR + IBL](pbr-ibl/index.md) | 打开 HDR/PBR/IBL 金属球场景，理解 scene-level IBL resources |
| 6 | [Offline Renderer](offline-renderer/index.md) | 用同一份 scene 跑 headless Vulkan compute，写出 EXR/PNG，并理解 path tracing 扩展点 |
| 7 | [扩展场景节点](extend-scene-node/index.md) | 理解一种新节点如何兼容 editor 全流程 |
| 8 | [扩展编辑器](extend-editor/index.md) | 理解 toolbar 与 command bus 如何配合 |

前六条学习线偏当前实践：构建、启动、保存场景、写材质、调灯光、观察 shadow/CSM、验证 PBR/IBL、运行离线输出。最后两条学习线会同时讲“当前手工路径”和“未来应该沉淀成扩展点的路径”。

## 当前可实践的路径与未来扩展路径

| 未来路径 | Requirement |
|---|---|
| 光源资产与自定义光源注册 | [REQ-042-a](../requirements/pending/042-a-tutorial-light-asset-and-custom-light-registry.md) |
| toolbar / command 扩展注册 | [REQ-042-b](../requirements/pending/042-b-tutorial-editor-extension-registry.md) |
| 自定义场景节点注册 | [REQ-042-c](../requirements/pending/042-c-tutorial-custom-scene-node-registry.md) |

凡是教程描述当前还没有的顺滑工作流，都会明确链接到 active requirement。这样我们能讲清楚设计方向，同时不把未来能力伪装成当前已经存在的 API。

## 按目标选择阅读顺序

| 目标 | 建议路径 |
|---|---|
| 只想先跑起来 | 启动项目 01 到 03 |
| 想做一个能看见的材质实验 | 启动项目全部 + 自定义材质全部 |
| 想理解 light 和 shader 的连接 | 自定义材质 01/02 + 自定义灯光 01/02/05 |
| 想理解 v0.1.1 的 shadow 主线 | 启动项目全部 + Shadow 阶段全部 |
| 想理解 PBR + IBL 的当前闭环 | 启动项目全部 + PBR + IBL 全部 |
| 想跑离线渲染或准备写 path tracing | 启动项目 01 + PBR + IBL 01 + Offline Renderer 全部 |
| 想新增一种场景对象 | 启动项目 03/04 + 扩展场景节点 |
| 想改 editor 操作入口 | 启动项目 04 + 扩展编辑器 |

我们不要求一开始理解所有底层 Vulkan 细节。先把一条链路跑通，再回头读 [场景系统](../scene-system/index.md) 和 [源码分析](../source_analysis/index.md)，会更稳。
