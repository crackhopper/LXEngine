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

### RenderingItem：一帧 draw 的最小稳定记录

这个结构体定义在 scene.hpp 而不是 queue.hpp，是因为它描述的是 backend 真正消费的契约，
而不是 queue 的内部状态。任何把"一个 renderable 在某个 pass 下要画一次"翻译成
"backend 提交单元"的代码路径，都收口到这个结构体上。

字段拆分体现两个边界：

- `shaderInfo / pipelineKey / pass`：决定走哪条 pipeline，是 pipeline cache 的 key 来源
- `vertexBuffer / indexBuffer / drawData / descriptorResources`：决定这次 draw 的数据来源
- `material`：保留材质句柄是为了 `PipelineBuildDesc::fromRenderingItem` 反查 render state
  和 owned binding 表，而不是 backend 直接读它

descriptorResources 的列表已经合并了"renderable 自带"和"scene-level 追加"两段，
顺序固定 — backend 按 binding name 命中，不依赖位置。

### Scene：扁平容器与默认 seed

Scene 是一层薄壳：三个平铺 vector（renderables / cameras / lights）+ 一个 sceneName。
它不维护层级（节点之间的 parent/child 关系挂在 SceneNode 上）、不做 z-sort、不持有
render state。这种扁平 ownership 让"哪些对象属于这一帧"是可枚举的事实，而不是
需要遍历某种隐式树才能复原的状态。

构造时强制 seed 一个 Camera + DirectionalLight，是 REQ-009 的兜底：那些不走完整
`VulkanRenderer::initScene` 的 core/test 路径，仍然能拿到非空的 scene-level 资源。
seed 出来的 Camera 已经 `setTarget(RenderTarget{})`，否则 `matchesTarget` 永远 false，
`getSceneLevelResources` 永远返回空 — 这条隐含约束容易在写测试时被忘掉。

`enable_shared_from_this` 的存在是为了在 `addRenderable` 里给挂进来的 SceneNode 写
弱反向引用 `weak_from_this()`，让 shared material 重验证传播能从 node 找回 scene。

## scene.cpp

源码位置：[scene.cpp](../../../../src/core/scene/scene.cpp)

### ~Scene：weak detach 协议

析构时显式遍历 renderables 并对每个 SceneNode 调 `detachFromScene()`，把 node 内
的 `m_scene` weak_ptr 清空。看起来冗余 — Scene 析构后，weak_ptr 本来就锁不回去。
但显式 reset 的目的不是断引用，而是让 SceneNode 后续的判断 "我现在还挂在某个
scene 上吗" 用 `m_scene.lock() != nullptr` 就能给出确定答案，不会出现 "持有的
是 expired weak，曾经挂过但 scene 已经销毁" 这种二义状态。

### revalidateNodesUsing：shared material 的结构性传播

多个 SceneNode 可以共享同一个 `MaterialInstance`。当材质本身的 pass 启用集合
（`setPassEnabled`）改变时，每个引用它的节点都需要重建 validated cache，因为
`supportsPass` 的结果会变。这条信号节点自己感知不到 — 节点不订阅材质事件，
所以由 Scene 在材质回调里集中遍历，按指针相等而不是 by-name 比较来匹配，
避免误伤同名不同实例的材质。

普通参数写入（`setFloat` / `setTexture`）走 GPU 资源 dirty 路径，结构没变，
不会触发这条传播。换句话说：这里只处理"pass 拓扑改变"这一件结构性事件。

### getSceneLevelResources：camera×target 与 light×pass 两轴筛选

REQ-009 的核心设计：camera 按 target 选，light 按 pass 选 — 两条规则有意拆开，
不合并成"同时过 pass 和 target"。原因来自身份的不同：

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

1. 先看 **`Scene` 与默认 seed**，建立"扁平容器 + REQ-009 兜底"这个心智模型。
2. 再看 **`addRenderable`**，理解 scene 在节点接入时强制注入的三件事 —
   nodeName 唯一、scene_debug_id、weak 反向句柄。
3. 然后跳到 **`getSceneLevelResources` 两轴筛选**，这是整个文件设计上最非平凡的一段，
   也是 REQ-009 落地的核心。
4. 最后用 **`getCombinedCameraCullingMask`** 收尾，理解资源筛选和可见性裁剪
   为什么要解耦。

`RenderingItem` 那一节单独看 — 它解释的是 scene.hpp 为什么承担"frame-consumed
draw record"的定义责任，与 Scene 类自身的运行时行为无直接耦合。

## Scene 与 SceneNode 的责任划分

读这一页时容易把"Scene 做的事"和"SceneNode 做的事"混在一起。一个粗略对照：

| 维度 | Scene 持有 | SceneNode 持有 |
|---|---|---|
| 命名空间 | nodeName 唯一性 | 自己的 nodeName 字符串 |
| 调试身份 | 注入 `<sceneName>/<nodeName>` 到 node | 接收并保存 sceneDebugId |
| 结构验证 | 只在 shared material pass 拓扑变化时触发节点重建 | 自己的 `rebuildValidatedCache()` |
| 层级 | 不感知 | parent / children / world transform |
| 资源 | 选 scene-level（camera UBO / light UBO） | 选 per-renderable descriptor |
| 可见性 | 合并 camera mask | 自己的 visibility mask |

简言之：**Scene 只做"全场景才能决定"的事**，其它都下放。

## 与 RenderQueue 的边界

`RenderQueue::buildFromScene(scene, pass, target)` 调 Scene 的入口只有两条：
`getSceneLevelResources(pass, target)` 和 `getCombinedCameraCullingMask(target)`。
其它一切（`shaderInfo`、`pipelineKey`、`descriptorResources` 中 per-renderable 的部分）
都直接走 `IRenderable::getValidatedPassData(pass)`。

这条接口边界让 Scene 和 RenderQueue 的契约非常窄 —
queue 不需要知道 SceneNode 内部 cache 形态，scene 也不需要知道 queue 排序策略。
任何想替换 queue 实现的工作只要遵守这两条 scene-side 接口加上 `IRenderable` 即可。

## REQ-034 落地后会变什么

[`REQ-034`](../../../../requirements/034-render-target-desc-and-target.md) 把
`RenderTarget` 拆为 `RenderTargetDesc`（形状） + `RenderTarget`（持 desc + 句柄
+ extent）后，本页的 target 轴叙事会同步变化：

- **接口签名同步**：`getSceneLevelResources(pass, target)` 与
  `getCombinedCameraCullingMask(target)` 的 `target` 参数从
  `const RenderTarget &` 改为 `const RenderTargetDesc &`。Scene 只关心 *形状*
  来做兼容性筛选，不需要持有 attachment 句柄。
- **`Camera::matchesTarget` 改语义**：`Camera::m_target` 从
  `optional<RenderTarget>` 改为 `optional<RenderTargetDesc>`，nullopt 表示通配。
  默认 seed Camera 仍然兜底（默认构造的 `RenderTargetDesc{}` 与默认 pass 的
  desc 相等），REQ-009 的 R7 不破坏。
- **target 轴变得有真实负载**：当前 target 轴几乎不做事 — 所有 pass 与 seed
  Camera 用的都是默认 RenderTarget，`matchesTarget` 永远 true。REQ-034 R2 让
  RenderTargetDesc 长出 MRT、stencil、layer 后，多 swapchain / 多 attachment
  format 的工程会让 target 轴产生真实的过滤行为；scene-level 资源筛选和
  可见性掩码合并才会在不同 desc 下走出不同分支。
- **依然不引入身份绑定**：REQ-034 R6 选的是 *形状兼容*（Q5 选项 A），不是
  身份绑定。多窗口 / 多 swapchain 想让 camera 专画某个 framebuffer，需要走
  visibility layer mask + pass support 这条侧路，不通过 target 轴表达。

本页 `getSceneLevelResources：camera×target 与 light×pass 两轴筛选` 一节的
*结构* 在 REQ-034 落地前后都成立；需要替换的只是"target 类型 = `RenderTarget`"
这条隐含假设。
