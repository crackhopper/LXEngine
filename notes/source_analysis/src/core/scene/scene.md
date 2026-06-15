# Scene：场景容器与 scene-level 资源筛选

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/scene/scene.hpp](../../../../../src/core/scene/scene.hpp)
和它的实现
[src/core/scene/scene.cpp](../../../../../src/core/scene/scene.cpp)
出发，关注的不是 API 列表，而是 `Scene` 为什么是一层薄壳：
把结构验证下放给 `SceneNode`、把 draw 组装下放给 `RenderQueue`，
自己只保留 nodeName 唯一性、shared material 重验证传播、
以及 scene-level 资源的两轴筛选这三件无法下放的事情。

可以先带着一个问题阅读：为什么 `Scene` 的容器是平铺的、构造时还要硬塞
一个默认 Camera 和 DirectionalLight？答案是 REQ-009 — 让那些不走完整
`VulkanRenderer::initScene` 的纯 core/test 路径仍然能拿到非空的
scene-level 资源，同时把 hierarchy/可见性等可选维度整体下推给 SceneNode。

源码入口：[scene.hpp](../../../../src/core/scene/scene.hpp)

关联源码：

- [scene.cpp](../../../../src/core/scene/scene.cpp)

## scene.hpp

源码位置：[scene.hpp](../../../../src/core/scene/scene.hpp)

### RenderWorkItem：一次 pipeline work 的最小稳定记录

这个结构体定义在 scene.hpp 而不是 queue.hpp，是因为它描述的是 backend
真正消费的契约，而不是 queue 的内部状态。任何把"一个 pass 内需要执行的一份
GPU work"翻译成 backend 提交单元的代码路径，都收口到这个结构体上。

字段拆分体现两个边界：

- `domain / kind / shaderInfo / pipelineKey / pass / target`：决定走哪条
  pipeline，以及这份 work 是 raster draw、compute dispatch 还是后续 RT work
- `descriptorResources`：决定 pipeline-visible 资源，顺序固定但 backend 按
  binding name 命中，不依赖位置
- `raster / compute`：按 work kind 存放特化 payload，避免把 raster-only
  vertex/index/material index 当成所有 render work 的公共字段
- `material`：保留材质句柄是为了 `PipelineBuildDesc::fromRenderWorkItem`
  不再保存材质对象；pipeline 需要的 render state 在 SceneNode 校验阶段复制进
  work item，材质资源绑定则由 scene descriptor resolver 从 SceneResourceTable
  解析。

### Scene：扁平容器

Scene 是一层薄壳：三个平铺 vector（renderables / cameras / lights）+ 一个
sceneName。 它不维护层级（节点之间的 parent/child 关系挂在 SceneNode 上）、不做
z-sort、不持有 render state。这种扁平 ownership
让"哪些对象属于这一帧"是可枚举的事实，而不是 需要遍历某种隐式树才能复原的状态。

Scene 本身不隐式创建 camera 或 light；测试和 demo 需要显式注册带组件的
SceneNode。

`enable_shared_from_this` 的存在是为了在 `addRenderable` 里给挂进来的 SceneNode
写 弱反向引用 `weak_from_this()`，让 shared material 重验证传播能从 node 找回
scene。

## scene.cpp

源码位置：[scene.cpp](../../../../src/core/scene/scene.cpp)

### ~Scene：weak detach 协议

析构时显式遍历 renderables 并对每个 SceneNode 调 `detachFromScene()`，把 node 内
的 `m_scene` weak_ptr 清空。看起来冗余 — Scene 析构后，weak_ptr 本来就锁不回去。
但显式 reset 的目的不是断引用，而是让 SceneNode 后续的判断 "我现在还挂在某个
scene 上吗" 用 `m_scene.lock() != nullptr` 就能给出确定答案，不会出现 "持有的
是 expired weak，曾经挂过但 scene 已经销毁" 这种二义状态。

### getSceneLevelResources：camera×target 与 light×pass

两轴筛选 REQ-009 的核心设计：camera 按 target 选，light 按 pass 选 —
两条规则有意拆开， 不合并成"同时过 pass 和 target"。原因来自身份的不同：

- camera 的身份是"画到哪个 target"，与 pass 无关。同一个 camera 在 forward、
  depth-prepass、GUI 这三个写入同一 target 的 pass 里都该出现，pipeline 不同
  但相机 UBO 是同一份。
- light 的身份是"参与哪些 pass"，与 target 无关。一个 DirectionalLight 在所有
  写入它支持的 pass 的 RenderTarget 上都该照亮，让 light 也带 target 限制会
  退化成 per-RT 复制 light 实例。

返回顺序固定：先 cameras 再 lights，各自按容器插入序追加。queue 把这一段拼在
per-renderable descriptor 列表末尾 — backend 按 binding name 命中，不依赖位置。
空返回是合法的（pass 没有任何 light 参与时常见），调用方不应该把空当作错误。

### getCombinedCameraCullingMask：可见性裁剪与资源筛选解耦

queue 用这个合并 mask 决定 renderable 是否进入当前 queue（按位与 visibilityMask
不为 0）。它和 `getSceneLevelResources` 用的是同一条 target 过滤规则，但作用
维度完全独立：

- 资源筛选：决定 CameraUBO / LightUBO 是否进入 descriptor 表
- mask 合并：决定 renderable 是否参与 draw

两条路径解耦的结果是：即使 mask 把所有 renderable 都裁掉，CameraUBO 还是会被
绑定 — pass 的 fixed-function 阶段仍然依赖它，下一帧重新出现时 backend 不需要
重建 binding。"这一帧没东西画" 不会反向撤销 scene-level 资源契约。

合并使用按位 OR：多 camera 的 visibility 是并集语义（renderable 只要被任何一个
target 相关 camera 接受就保留），不是交集。

<!-- SOURCE_ANALYSIS:EXTRA -->

## 推荐阅读顺序

按下面这条线读最不容易踩空：

1. 先看 **`Scene` 与 camera-node 注册**，建立"扁平容器 + 显式 camera 注册"这个心智模型。
2. 再看 **`addRenderable`**，理解 scene 在节点接入时强制注入的三件事 —
   nodeName 唯一、scene_debug_id、weak 反向句柄；以及路径名重复时只告警不阻断。
3. 然后看 **`findByPath` / `dumpTree`**，它们解释为什么 Scene 需要一个 synthetic root，
   以及为什么 path 语义故意和 renderable 的 `nodeName` 解耦。
4. 最后跳到 **`getSceneLevelResources` 两轴筛选**，这是整个文件设计上最非平凡的一段，
   也是 scene-level 资源过滤的核心。
5. 最后用 **`getCombinedCameraCullingMask`** 收尾，理解资源筛选和可见性裁剪
   为什么要解耦。

`RenderWorkItem` 那一节单独看 — 它解释的是 scene.hpp 为什么承担"frame-consumed
work record"的定义责任，与 Scene 类自身的运行时行为无直接耦合。

## Scene 与 SceneNode 的责任划分

读这一页时容易把"Scene 做的事"和"SceneNode 做的事"混在一起。一个粗略对照：

| 维度 | Scene 持有 | SceneNode 持有 |
|---|---|---|
| 命名空间 | nodeName 唯一性 + synthetic path root + `findByPath()` | 自己的 nodeName 字符串 + 独立 `name/path` |
| 调试身份 | 注入 `<sceneName>/<nodeName>` 到 node | 接收并保存 sceneDebugId |
| 结构验证 | 只在 shared material pass 拓扑变化时触发节点重建 | 自己的 `rebuildValidatedCache()` |
| 层级 | 不感知 | parent / children / world transform |
| 资源 | 选 scene-level（camera UBO / light UBO） | 选 per-renderable descriptor + 持有 camera component |
| 可见性 | 合并 camera mask | 自己的 visibility mask |

简言之：**Scene 只做"全场景才能决定"的事**，其它都下放。

## 路径命名空间补充

REQ-036 之后，`Scene` 同时维护两套不同目的的名字：

- `nodeName`：渲染/调试身份，仍要求 scene 内唯一，重复直接抛错
- `name`：给 editor、命令总线、调试树使用的路径段，允许为空，也允许 sibling 重名

为了让 `/world/player/arm` 这类路径有一个稳定根，`Scene` 内部额外持有一个
synthetic root node。它不参与 renderable ownership，也不出现在 `m_renderables`
里，只服务三条路径侧 API：

- `findByPath("/")`：返回 synthetic root
- `findByPath("/world/player")`：从 synthetic root 向下按 child 顺序首匹配
- `dumpTree()`：从 synthetic root 打印 box-drawing 文本树

这套设计保留了原有 parent/child 存储，不引入 path cache。`setParent()` 后下一次
`getPath()` 直接沿 parent 链现算，因此路径更新没有额外失效协议。

空名节点不会让路径系统失效：`getPath()` 和 `dumpTree()` 会把该段显示成
`<unnamed-node-0xADDR>` 占位；`findByPath()` 同时接受真实空段（`//`）和这个占位
文本作为匹配键，保证 dump 出来的路径仍可反查回节点。

## 与 RenderQueue 的边界

`RenderWorkQueue::build(...)` 调 Scene 的入口主要是：
`getSceneLevelResources(pass, target)` 和 `getCombinedCameraCullingMask(target)`。
其它一切（`shaderInfo`、`pipelineKey`、`descriptorResources` 中 per-renderable 的部分）
都直接走 `IRenderable::getValidatedPassData(pass)`。

这条接口边界让 Scene 和 RenderQueue 的契约非常窄 —
queue 不需要知道 SceneNode 内部 cache 形态，scene 也不需要知道 queue 排序策略。
任何想替换 queue 实现的工作只要遵守这两条 scene-side 接口加上 `IRenderable` 即可。

## 当前 target 轴状态

`FramePass` 使用 `RenderTargetDesc` 表达输出形状，`RenderWorkQueue::build` 仍通过兼容 `RenderTarget{pass.target}` 调用 scene 资源筛选。Scene 只关心 camera 是否匹配 target 形状，不持有 backend attachment 句柄。

这意味着 target 轴已经有真实 offscreen/depth-only/HDR 形状语义，但 framebuffer、image view、layout transition 仍属于 backend 执行层。
