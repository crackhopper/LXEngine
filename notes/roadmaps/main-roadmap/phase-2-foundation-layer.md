# Phase 2 · 基础层 + 文本内省

> **目标**：让引擎拥有"真正的世界"抽象（物体有父子关系、有本地/世界坐标）、"真正的交互"能力（键盘/鼠标/手柄输入、稳定的 delta time），并让引擎所有内部状态都能以**结构化文本**的形式输出，为 AI-Native 的 Phase 10 MCP + Agent 做铺垫。
>
> **依赖**：现状即可启动，与 Phase 1 可并行。
>
> **可交付**：
> - `demo_transform_input` — WASD + 鼠标自由飞行相机，视窗里一个有父子层级的小场景
> - `engine-cli dump-scene foo.json --format=tree` — 把任意场景打成文本树输出，LLM 可直接读

## 范围与边界

**做**：
- Transform 组件（本地 T/R/S + 世界矩阵 + dirty 标记）
- Scene 节点 + 父子层级 + 遍历 API
- 输入抽象层（Keyboard / Mouse / Gamepad）
- Action mapping 层
- Time 模块（real time / game time / delta / fixed step 累加器）
- 通用 game loop 骨架
- **文本内省 API**：`dumpScene()` / `describe()` 族的结构化输出
- **AABB + 空间索引**：为 spatial query 和 dump 提供坐标基础

**不做**：
- 组件生命周期 / 脚本（→ Phase 6）
- 物理更新（→ Phase 5）
- 序列化（→ Phase 3）
- MCP tool 注册（→ Phase 10，会直接复用本阶段的 dump API）

---

## 前置条件

- `Scene` 已经持有 `m_renderables`（会被改造成树）
- `Camera` 已经有 `position / target / up`（会迁移到 `Transform`）

---

## 工作分解

### REQ-201 · Transform 组件

新增 `src/core/scene/transform.hpp`：

```cpp
class Transform {
public:
    Vec3f  localPosition{0, 0, 0};
    Quatf  localRotation = Quatf::identity();
    Vec3f  localScale{1, 1, 1};

    Mat4f getLocalMatrix() const;
    Mat4f getWorldMatrix() const;  // 查 m_worldMatrix 缓存

    void setDirty();
    bool isDirty() const;

private:
    mutable Mat4f m_worldMatrix = Mat4f::identity();
    mutable bool  m_dirty = true;
    Transform*    m_parent = nullptr;
};
```

- 需要 `Quatf` 类型（`core/math/quat.hpp`），若没有先补
- World matrix 的计算是懒惰的：`getWorldMatrix()` 时若 dirty 则重算
- `setDirty()` 递归标记所有子节点为 dirty
- 修改 local TRS 自动触发 `setDirty`

**验收**：`test_transform.cpp` 集成测试：构造父子链、移动父、读取子的 world matrix 等于父 world × 子 local。

### REQ-202 · SceneNode 层级

重构 `Scene`：

```cpp
class SceneNode {
public:
    Transform transform;
    StringID  name;
    std::vector<std::shared_ptr<SceneNode>> children;
    std::weak_ptr<SceneNode> parent;

    // 可选挂载
    IRenderablePtr  renderable;
    CameraPtr       camera;
    LightBasePtr    light;
    // 后续阶段新增：AnimationPlayerPtr, RigidBodyPtr, ScriptPtr ...
};

class Scene {
    SceneNodePtr m_root;
    // 废弃旧的 vector<IRenderablePtr>，改成 traversal
};
```

- `RenderQueue::buildFromScene` 从 root 遍历到每个叶子，收集 `renderable != nullptr` 的节点
- Transform 的 world matrix 取代外部塞 `PC_Draw.model` 的做法
- 保留多相机 / 多光源语义：遍历收集所有 `camera != nullptr` 和 `light != nullptr` 的节点
- 迁移 `src/test/test_render_triangle.cpp` / tutorial 的 cube demo 到新 API

!!! warning "这是破坏性重构"
    `Scene` 构造函数、`getRenderables()` / `getCameras()` / `getLights()` 的返回类型都会变。需要一个明确的 migration 节点 + 所有示例同步更新。

**验收**：`demo_transform_input` 里一个父节点（行星）带动三个子节点（卫星）一起旋转。

### REQ-203 · Input 状态

新增 `src/core/input/input_state.hpp`（接口）+ `src/infra/input/sdl3_input.hpp`（实现）：

```cpp
class IInputState {
public:
    virtual bool isKeyDown(KeyCode code) const = 0;
    virtual bool isKeyPressed(KeyCode code) const = 0;   // 刚按下这一帧
    virtual bool isKeyReleased(KeyCode code) const = 0;

    virtual Vec2f getMousePosition() const = 0;
    virtual Vec2f getMouseDelta() const = 0;
    virtual bool  isMouseButtonDown(MouseButton b) const = 0;
    virtual float getMouseWheelDelta() const = 0;

    virtual void nextFrame() = 0; // 清 per-frame 状态
};
```

- `KeyCode` 是一个 enum，内部映射 SDL3 scancode
- `sdl3_input.hpp` 在 `Window::pollEvents` 回调里更新状态
- `Window` 构造注入 `InputState`（构造函数注入，不走 setter）

**验收**：`test_input.cpp`：模拟 SDL 事件 → 查询状态一致。

### REQ-204 · 手柄支持

SDL3 已经带 gamepad API。

- `IGamepadState` 接口 + `Sdl3GamepadState` 实现
- 支持轴（左/右摇杆、扳机）+ 按键 + 震动
- 多个手柄按索引访问

**验收**：`demo_transform_input` 接手柄后左摇杆也能驱动相机。

### REQ-205 · Action Map

游戏代码不应该直接问 `isKeyDown(KeyCode::W)`，而是问 `isAction("MoveForward")`。

```cpp
class ActionMap {
public:
    void bind(StringID action, KeyCode key);
    void bind(StringID action, GamepadButton button);
    void bind(StringID action, MouseButton button);
    void bindAxis(StringID action, GamepadAxis axis);

    bool  isActive(StringID action) const;
    float getValue(StringID action) const;  // 0–1 或 -1–1
};
```

- 可以从 JSON / YAML 加载（依赖 Phase 3，但接口先定）
- 先硬编码 default binding，Phase 3 后再做文件加载

**验收**：自由飞行相机代码里只引用 StringID action 名字，不引用具体 key。

### REQ-206 · Time 模块

新增 `src/core/time/clock.hpp`：

```cpp
class Clock {
public:
    void tick();                     // 每帧开头调一次

    float  deltaTime()       const;  // 上一帧的时长（秒）
    double totalTime()       const;  // 启动以来总时长
    float  fixedDeltaTime()  const;  // 默认 1/60
    int    fixedStepsToRun() const;  // 本帧应该跑几次 fixed update

    float  timeScale() const;
    void   setTimeScale(float s);

    uint64_t frameCount() const;
};
```

- `fixedStepsToRun()` 用 accumulator 模式（每帧累加 `deltaTime()`，消耗 `fixedDeltaTime()` 次数）
- `timeScale` 影响 `deltaTime` 输出但不影响真实时间（暂停 = 0）

**验收**：`test_clock.cpp` 模拟跳变帧时间，验证 fixed step 累加不漂移不漏帧。

### REQ-207 · Game Loop 骨架

提炼一个新的 `Application` 或 `Engine` 类，取代目前 `main` 里手写的 while 循环：

```cpp
class Engine {
public:
    void run();

protected:
    virtual void onInit() {}
    virtual void onUpdate(float dt) {}
    virtual void onFixedUpdate(float fdt) {}
    virtual void onRender() {}
    virtual void onShutdown() {}

private:
    // 持有 window / input / time / renderer / scene
};
```

标准 loop：

```cpp
while (running) {
    input.nextFrame();
    window.pollEvents();
    clock.tick();

    for (int i = 0; i < clock.fixedStepsToRun(); ++i)
        onFixedUpdate(clock.fixedDeltaTime());

    onUpdate(clock.deltaTime());
    onRender();

    renderer.uploadData();
    renderer.draw();
}
```

- 用户代码继承 `Engine` 实现回调
- tutorial 的 `test_pbr_cube.cpp` 也迁移到这个形态（作为验收用例）

**验收**：所有示例程序的 `main` 函数都 ≤ 15 行，只构造 Engine 子类并 `run()`。

### REQ-208 · FreeFly Camera 控制器

作为 REQ-201 ~ 207 的集成 demo，写一个 `FreeFlyCameraController`：

- WASD 前后左右 / Space E 上下 / Shift 加速
- 鼠标右键按下时锁定鼠标，滑动控制 yaw / pitch
- 手柄左摇杆移动 + 右摇杆转动
- 全走 ActionMap

**验收**：`demo_transform_input` 跑起来能自由漫游场景。

---

## 文本内省 + 事件流工作分解（AI-Native 核心）

本节是 AI-Native 版本 roadmap 新增的内容。要让 Phase 10 的 agent 能理解引擎状态，必须让引擎的每一层都有"给人读也给 LLM 读"的文本输出通道 —— 同时，要为后续的 time travel / undo / replay / network sync 打下基础，必须把所有写入统一成事件流。

**本节落实的原则**：[P-1 确定性](principles.md#p-1-确定性是架构级不变量) · [P-2 事件流](principles.md#p-2-状态即事件流) · [P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语) · [P-5 语义查询](principles.md#p-5-语义查询层) · [P-7 多分辨率观察](principles.md#p-7-多分辨率观察--渐进披露) · [P-12 错误即教学](principles.md#p-12-错误即教学) · [P-19 命令总线](principles.md#p-19-bi-directional-命令总线)

### REQ-209 · 通用 `describe()` 契约

引入一个轻量级的 `IDescribable` 接口：

```cpp
class IDescribable {
public:
    virtual ~IDescribable() = default;

    /// 结构化输出。format 支持 "json" / "yaml" / "tree" / "pretty"。
    /// 注意：不是 toString() 那种调试字符串，而是 agent 可消费的结构化文本。
    virtual std::string describe(std::string_view format = "json") const = 0;
};
```

`Transform`、`SceneNode`、`Camera`、`LightBase`、`IRenderable`、`MaterialInstance`、`IShader`、`IGpuResource` 全部实现它。大部分实现只需要反射 + 一个 `nlohmann/json` 往外写的模板。

**验收**：对任意一个 `SceneNode` 调 `describe("json")` 能得到可 `json::parse` 的字符串。

### REQ-210 · 多分辨率 dump API

契合 [P-7 渐进披露](principles.md#p-7-多分辨率观察--渐进披露)：永远不一次性返回全部状态。

每个 dump 函数接受 **resolution** 参数，返回对应粒度：

| resolution | 典型大小 | 用途 |
|-----------|---------|------|
| `summary` | 1–3 行 | agent 一句话确认状态 |
| `outline` | 结构，无数值 | 了解层级 / 命名 |
| `full` | 全部字段 | 明确需要细节时 |

接口形态（伪签名，实际 API 名以命名约定为准）：

```
introspect.dumpScene     (resolution, filter?, pagination?)
introspect.dumpNode      (path, resolution, depth?)
introspect.dumpSpatial   (resolution, region?)
introspect.dumpResource  (handle, resolution)
introspect.dumpFrameGraph(resolution)
```

所有 dump 都支持以下公共参数：
- `format`：`tree` / `json` / `yaml` 等
- `filter`：只输出匹配某个 pattern / tag / 类型的子集
- `pagination`：返回 continuation token，agent 可以继续拉下一批
- `hint`：在返回里自动附上"下一步可以做什么"的提示（P-7 drill-down hint）

**summary 示例**（典型场景 1 行）：

```
Scene: 127 nodes, 3 cameras, 12 lights, 4 active anims; last event #342 2s ago
```

**outline 示例**（仅结构）：

```
Scene (root)
├── Camera "main"
├── DirectionalLight "sun"
└── player
    ├── mesh_body
    └── weapon
```

**full 示例**：包含 transform 数值、UBO 内容、pipeline key 等。

**验收**：对 tutorial 的 cube 场景调用 `dumpScene(resolution=summary)` 得到一行，`dumpScene(resolution=full)` 得到可完整重建场景的 JSON。

典型 `tree` 输出示例：

```
Scene (root)
├── Camera [pos=(0,1,3) fov=45°]
├── DirectionalLight [dir=(-0.4,-1.0,-0.3) color=(1,1,1)]
└── player (SceneNode)
    ├── Transform [pos=(0,0,0) rot=(0,0,0,1) scale=(1,1,1)]
    ├── Renderable [mesh=cube material=pbr_cube]
    │   └── Material uniforms:
    │       ├── baseColor = (0.85, 0.20, 0.20)
    │       ├── metallic  = 0.0
    │       ├── roughness = 0.35
    │       └── ao        = 1.0
    └── weapon (SceneNode)
        ├── Transform [pos=(0.3,0.1,0) ...]
        └── Renderable [mesh=sword material=pbr_metal]
```

典型 `json` 输出对应结构化对象，字段命名和 `tree` 对应。

**验收**：对 tutorial 的 `demo_pbr_cube` 场景调 `dumpScene("tree")` 得到一个和上面示例格式一致的树。

### REQ-211 · 结构化错误路径

把所有可能的错误信息改造成"带路径 + 可定位"的结构：

```cpp
struct EngineError {
    std::string_view category;   // "missing_resource" / "type_mismatch" / ...
    std::string      path;       // "scene.root/player/arm/weapon" or "material.uniforms.baseColor"
    std::string      message;    // human-readable
    std::optional<std::string> fix;  // "set baseColor via material.setVec3(...)"
};
```

- 替换现有 `throw std::runtime_error(...)` 的裸抛点
- 错误可以 JSON 序列化
- 错误可以被 agent 直接作为上下文传回 LLM

**验收**：故意给材质 loader 传一个错的 shader 路径，返回的 `EngineError.path` 指向具体位置而不是裸 filename。

### REQ-212 · 空间索引（AABB + 可选 BVH）

- 每个 `SceneNode` 提供 `getWorldAABB()`（取自 mesh AABB × world transform）
- 提供一个场景级 `SpatialIndex`：给一个 AABB / ray / point，返回命中节点列表
- Phase 1 的 frustum culling 可以走这个索引
- Phase 5 的物理查询（非碰撞检测，而是 "find nodes in region"）也用这个
- Phase 10 的 agent query 也用它："找场景里所有在玩家周围 5 米的物体"

**验收**：在一个 1000 节点的场景里，`findInRadius(playerPos, 5.0f)` 在 O(log n) 里返回正确结果。

### REQ-213 · 命令层：三层 API 的写入路径

Dump 是只读的 query layer。本 REQ 建立**命令层**，这是引擎**唯一的写入入口**（除了渲染热路径的 primitive layer）。契合 [P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语)。

**命令的最小承诺**：

1. **事务性**：失败则状态不变
2. **可逆性**：每个命令声明对应的反向命令（或由引擎自动生成反向 diff）
3. **可序列化**：命令参数 + 结果都是结构化对象
4. **可预演**：支持 dry-run（见 REQ-214），契合 [P-8](principles.md#p-8-dry-run--影子状态)
5. **产生事件**：每条命令成功执行后产出一条 `Event`，进入全局事件流（REQ-215）
6. **带成本估算**：契合 [P-9](principles.md#p-9-成本模型是一等公民)
7. **带确认级别**：auto / notify / confirm / review 四档，契合 [P-13](principles.md#p-13-human-in-the-loop-是类型级契约)

**命令注册的单源原则**（[P-4](principles.md#p-4-单源能力清单capability-manifest)）：

一条命令的声明只写一次，自动派生出：
- MCP tool schema
- TypeScript 类型定义
- CLI 帮助文本
- 编辑器 inspector 字段
- 测试 fixture

**首批命令分组**：
- `scene.*`：节点创建 / 删除 / 移动 / 查询
- `component.*`：组件挂载 / 卸载 / 字段设置
- `query.*`：语义查询（REQ-216）
- `dev.*`：dump / profile / 日志

具体命令清单不在本 roadmap 中冻结 —— 命令集是会演进的，遵循 Phase 10 REQ-1002 的 skill registry 机制动态增长。

**验收**：
- 任意命令通过命令总线调用生效
- 调用命令自动产生一条 Event 进入事件流
- 命令的 schema 能被 `capability.list()` 查到
- 失败的命令不留下任何状态修改

### REQ-214 · Dry-run / 影子执行

契合 [P-8](principles.md#p-8-dry-run--影子状态)：

```
command.run(params)     → Result (commits)
command.preview(params) → PredictedDiff (does not commit)
```

- `preview` 在影子状态上执行命令、收集所有会产生的事件、然后丢弃影子
- 返回结构包含：预测事件列表 / 预测成本 / 可能产生的警告 / 是否需要确认
- Agent 的正确工作流：query → preview → 检查 diff → run

**实现策略（抽象）**：
- 轻量方案：copy-on-write 的状态视图，命令在视图上执行后对比差异
- 重量方案：持久化数据结构，任意"分支"都能被探索

**验收**：对同一个命令，`preview` 和 `run` 产生的事件列表等价；`preview` 不留下任何真实副作用。

### REQ-215 · 事件流

契合 [P-2](principles.md#p-2-状态即事件流)：

```
Event {
    id:           monotonically increasing
    timestamp:    absolute + logical
    schema_version: int
    command:      reference to the command that produced it
    before_hash:  state hash at the time of issuing
    after_hash:   state hash after applying
    payload:      typed diff describing what changed
    cause:        optional reference to an Intent / parent event
}
```

- 所有写入（命令、资产加载完成、异步生成结果返回等）都表达为事件
- 事件追加到引擎内全局的 **event log**
- 事件 log 可订阅（P-19 命令总线的广播机制）：编辑器、外部 agent、未来的网络同步都订阅这里
- 每隔 N 个事件做一次**状态快照**，加速 replay 和 time-travel 查询
- 事件 schema 带版本号（P-15 重构友好）

**验收**：
- 执行 100 条命令后，从 initial state + 100 条 event 能 replay 出完全相同的 state
- 任意两个 event id 之间可以 diff
- 事件 log 可以持久化到磁盘 + 跨 session 加载

### REQ-216 · 语义查询入口

契合 [P-5](principles.md#p-5-语义查询层)：

一个统一的 `query.select(...)` 入口，接收一个描述"意图过滤"的结构，返回匹配结果：

- 按**类型**过滤：所有 `RigidBody` / 所有 `Light`
- 按**标签**过滤：所有带 `enemy` tag
- 按**空间关系**过滤：在某个 AABB / 球 / 视锥内
- 按**组件字段值**过滤：`health < 30`
- 按**关系**过滤：`material references texture xxx`
- 按**命名模式**过滤：glob / regex
- 可组合（and / or / not）

返回结果的分辨率由调用方指定（REQ-210）：id 列表 / outline / full。

**不实现**的部分：复杂的聚合 / group by / 子查询 —— 80% 常见情况够用即可，复杂场景留给 agent 拼命令。

**验收**：`query.select({type:"node", where:[{has:"light"},{in_radius:{center:[0,0,0],r:10}}]})` 返回场景里距离原点 10 米内的所有光源节点。

### REQ-217 · 错误对象

契合 [P-12](principles.md#p-12-错误即教学)：

替换所有"裸字符串 exception"为结构化错误对象：

```
EngineError {
    code, path, message, reason, fix_hint, related, severity, agent_tip
}
```

- 可 JSON 序列化
- 可被 agent 作为下一轮 prompt 的上下文
- 必填字段：`code` / `message` / `fix_hint` / `path`
- `agent_tip` 是专门写给 LLM 的简短建议

**验收**：故意给命令传一个错路径 / 错类型参数，返回的错误对象包含完整 5 元组，agent 能根据 `fix_hint` 自己纠正。

---

## 里程碑

### M2.1 · Transform 成形

- REQ-201 + REQ-202 完成
- demo：一个 cube 挂在一个 root 下，root 移动 cube 跟着动

### M2.2 · 键鼠可用

- REQ-203 完成
- demo：窗口内按 ESC 退出（走 InputState 而不是 `onClose`）

### M2.3 · 时间 + 循环统一

- REQ-206 + REQ-207 完成
- demo：立方体以稳定 60 FPS 旋转（不再受刷新率漂移）

### M2.4 · 自由飞行

- REQ-204 + REQ-205 + REQ-208 完成
- demo：`demo_transform_input`，WASD + 鼠标 + 手柄全通

### M2.5 · 文本内省就位

- REQ-209 + REQ-210 + REQ-211 完成
- demo：多分辨率 dump 可按 `summary/outline/full` 切换

### M2.6 · 命令层 + 空间索引

- REQ-212 + REQ-213 完成
- demo：命令行对场景做增删改查操作，所有变更产出事件

### M2.7 · Dry-run + 事件流 + 语义查询 + 错误即教学

- REQ-214 + REQ-215 + REQ-216 + REQ-217 完成
- demo：`preview` 一条命令得到预测 diff → `run` → 事件进入 log → 通过 replay 100 条事件恢复状态完全一致

---

## 风险 / 未知

- **Transform 系统的脏标记层级**：子节点被移动后父没动，重算范围要精确，否则频繁失效。先用"父 dirty → 全子 dirty"的保守算法，Phase 3 再优化。
- **Scene 重构是破坏性的**：所有现存示例（`test_render_triangle.cpp`、tutorial）必须同步迁移。建议开一个 `req/` 分支实施，合并前把 tutorial 03/05 的 shader 和 code 也同步更新。
- **SDL3 的 gamepad API 在 Linux 上有些发行版默认没 udev 权限**。解决：给文档加一条 note。
- **鼠标锁定在 Wayland 上行为与 X11 不同**。Phase 2 内先支持 X11，Wayland 放到 Phase 9。
- **Dump 体量**：一个复杂场景的 JSON dump 可能几十 KB，对 LLM 来说不小。解决：`dumpScene` 接 `depth` / `filter` 参数，默认只输出概要。
- **Command schema 手写负担**：首批命令的 schema 手写不重，但几十条后成本可观。解决：Phase 6 的反射元数据机制会自动生成组件类的 schema，届时迁移。

---

## 与现有架构的契合

- `core/` / `infra/` / `backend/` 三层规则适配良好：`Transform` / `SceneNode` / `Clock` 纯 core，`InputState` 是 core 接口 + infra 实现。
- `StringID` 被 ActionMap 直接复用。
- `Window` 已经是接口，构造注入 `InputState` 符合 R5。
- `Scene` 的 multi-camera / multi-light vector 迁移到 tree traversal 时，`getSceneLevelResources(pass, target)` 的过滤语义不变，只是数据源从 vector 变成 "遍历 tree 收集"。

---

## 与 AI-Native 原则的契合

本阶段是整份 roadmap 里**原则落实最密集的一个 phase**，因为 Phase 10+ 所有的 AI-Native 能力都站在本阶段的地基上：

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-1 确定性](principles.md#p-1-确定性是架构级不变量) | Clock 抽象 / fixed step accumulator / 命令层是唯一写入入口 |
| [P-2 事件流](principles.md#p-2-状态即事件流) | REQ-215 事件流 + schema 版本 |
| [P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语) | introspect 是 query layer / REQ-213 是 command layer / 原有渲染热路径保留为 primitive layer |
| [P-4 单源能力清单](principles.md#p-4-单源能力清单capability-manifest) | REQ-213 的命令声明自动产出各外部面 |
| [P-5 语义查询](principles.md#p-5-语义查询层) | REQ-216 提供统一的 `query.select` 入口 |
| [P-7 多分辨率](principles.md#p-7-多分辨率观察--渐进披露) | REQ-210 的 summary/outline/full 三档 dump |
| [P-8 Dry-run](principles.md#p-8-dry-run--影子状态) | REQ-214 |
| [P-12 错误即教学](principles.md#p-12-错误即教学) | REQ-217 |
| [P-15 版本化](principles.md#p-15-重构友好--版本化的一切) | 事件 / 命令 / dump schema 都带版本号 |
| [P-19 命令总线](principles.md#p-19-bi-directional-命令总线) | REQ-213 注册到全局总线，编辑器 / agent / 脚本共享 |

---

## 下一步

Phase 2 + Phase 1 合流后进入 [Phase 3 资产管线](phase-3-asset-pipeline.md)。在那之前，Phase 2 末尾的 `Scene` 已经是树形，为 Phase 3 的序列化提供了干净的起点。
