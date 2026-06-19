# Scene：场景容器与 scene-level 资源筛选

本页的主体内容由 `scripts/source_analysis/extract_sections.py` 从源码中的
`@source_analysis.section` 注释块生成，用来把讲解锚定在真实代码结构上。

这一页从
[src/core/scene/scene.hpp](../../../../../src/core/scene/scene.hpp)
和它的实现
[src/core/scene/scene.cpp](../../../../../src/core/scene/scene.cpp)
出发，关注的不是 API 列表，而是 `Scene` 为什么是一层薄壳：
把结构验证下放给 `SceneNode`，把 draw / dispatch payload 组装交给
`RenderWorkCompiler`，自己只保留 nodeName 唯一性、shared material
重验证传播、scene resource table 和 scene-level 资源筛选这些全场景事实。

可以先带着一个问题阅读：为什么 `Scene` 的容器是平铺的、构造时还要硬塞
一个默认 Camera 和 DirectionalLight？答案是现在的 `Scene` 只表达注册事实；
editor、offline loader 和测试都必须显式注册带组件的 node。可见性、hierarchy
和 renderable pass validation 留在 `SceneNode` / compiler 边界处理。

源码入口：[scene.hpp](../../../../src/core/scene/scene.hpp)

关联源码：

- [scene.cpp](../../../../src/core/scene/scene.cpp)

## scene.hpp

源码位置：[scene.hpp](../../../../src/core/scene/scene.hpp)

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

`SceneResourceTable` 相关章节可以单独看。它解释的是 scene 如何把 mesh、object、
material、camera、light 和 IBL bake facts 登记成 typed handle；这和 `Scene`
平铺容器的运行时职责相关，但不是同一个层级。

## Scene 与 SceneNode 的责任划分

读这一页时容易把"Scene 做的事"和"SceneNode 做的事"混在一起。一个粗略对照：

| 维度 | Scene 持有 | SceneNode 持有 |
|---|---|---|
| 命名空间 | nodeName 唯一性 + synthetic path root + `findByPath()` | 自己的 nodeName 字符串 + 独立 `name/path` |
| 调试身份 | 注入 `<sceneName>/<nodeName>` 到 node | 接收并保存 sceneDebugId |
| 结构验证 | 只在 shared material pass 拓扑变化时触发节点重建 | 自己的 `rebuildValidatedCache()` |
| 层级 | 不感知 | parent / children / world transform |
| 资源 | 维护 `SceneResourceTable`，并按 pass/target 选 scene-level camera/light 资源 | 持有 mesh/material/camera/light component，暴露 validated pass data |
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

## 与 RenderWorkCompiler 的边界

`RenderWorkCompiler::buildInputs(...)` 调 Scene 的入口主要是：
`getRenderables()`、`getSceneLevelResources(pass, target)` 和
`getCombinedCameraCullingMask(target)`。其它一切 per-renderable 结构事实来自
`IRenderable::getValidatedPassData(pass)`。

这条接口边界让 Scene 和 compiler 的契约保持窄而稳定：compiler 不需要知道
SceneNode 内部 cache 怎样失效，scene 也不需要知道 input 排序、diagnostic 或
pipeline desc 怎样生成。任何替换 compiler 的工作只要遵守这几条 scene-side 接口
和 `IRenderable` pass validation 出口即可。

## 当前 target 轴状态

`FramePass` 使用 `RenderTargetDesc` 表达输出形状，compiler 仍通过兼容
`RenderTarget{pass.target}` 调用 scene 资源筛选。Scene 只关心 camera 是否匹配
target 形状，不持有 backend attachment 句柄。

这意味着 target 轴已经有真实 offscreen/depth-only/HDR 形状语义，但 framebuffer、image view、layout transition 仍属于 backend 执行层。
