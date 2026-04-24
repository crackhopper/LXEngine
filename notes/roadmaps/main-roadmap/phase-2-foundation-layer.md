# Phase 2 · 基础层 + 文本内省

> **目标**：给引擎“真正的世界”抽象（物体有父子关系 + 本地/世界坐标）、“真正的交互”能力（键鼠 / 手柄 / action mapping / 稳定 delta time），并让引擎所有内部状态可以输出为**结构化文本**，为 Phase 10 MCP + Agent 做铺垫。
>
> **依赖**：现状即可启动，与 Phase 1 可并行。
>
> **可交付**：
> - `demo_transform_input` — WASD + 鼠标 FreeFly，视窗中一个有父子层级的小场景
> - `engine-cli dump-scene foo.json --format=tree` — 任意场景打成文本树，LLM 可直接读

## 当前实施状态（2026-04-24）

**部分开工**。时间 / 输入 / 相机 / UI 已落地；transform 层级 + 文本内省 API 仍是空白。

| 条目 | 状态 | 备注 / 位置 |
|------|------|-------------|
| `Clock`（tick / deltaTime / smoothedDeltaTime） | ✅ | `src/core/time/clock.*` |
| `EngineLoop`（`initialize` / `startScene` / `tickFrame` / `run` / `stop` / `requestSceneRebuild` / `setUpdateHook`） | ✅ | `src/core/gpu/engine_loop.*` |
| 输入抽象 `IInputState` + `KeyCode` + `MouseButton` | ✅ | `src/core/input/*` |
| `DummyInputState` / `MockInputState` 测试辅助 | ✅ | `src/core/input/dummy_input_state.hpp` / `mock_input_state.hpp` |
| SDL3 输入实现 `Sdl3InputState`（键鼠） | ✅ | `src/infra/window/sdl3_input_state.*` |
| Orbit / FreeFly 相机控制器 | ✅ | `src/core/scene/orbit_camera_controller.*` + `freefly_camera_controller.*` |
| ImGui overlay + debug panel helper | ✅ | `src/infra/gui/gui.hpp` + `imgui_gui.cpp` + `debug_ui.*` |
| `Transform` 组件 + 父子层级 | ❌ | REQ-201 / REQ-202 |
| Action mapping 层 | ❌ | REQ-205 |
| Gamepad 输入 | ❌ | REQ-204 |
| Fixed step 累加器 / 时间缩放 / 暂停 | ❌ | REQ-206 |
| `dumpScene` / `describe` 文本内省 API | ❌ | REQ-207 / REQ-208 |
| AABB + 空间索引 | ❌ | REQ-209 |

## 范围与边界

**做**：

- Transform 组件（本地 TRS + 世界矩阵 + dirty 标记 + 父子遍历）
- Scene 节点层级 + 插入/删除/查找 API
- 输入抽象层扩展：gamepad + action mapping
- Time 模块扩展：fixed step 累加器 + 暂停 + 时间缩放
- 通用 game loop 骨架增强（与 `EngineLoop` 对齐）
- **文本内省 API**：`dumpScene()` / `describe()` 族结构化输出
- **AABB + 空间索引**：为 spatial query 和 dump 提供坐标基础

**不做**：

- 组件生命周期 / 脚本（→ Phase 6）
- 物理更新（→ Phase 5）
- 序列化（→ Phase 3）
- MCP tool 注册（→ Phase 10，直接复用本阶段 dump API）

## 前置条件

- `Scene` 已持有 renderables（会被改造成树）✅
- `Camera` 已有 `position / target / up`（会迁移到 `Transform`）✅

## 工作分解

### REQ-201 · Transform 组件

新增 `src/core/scene/transform.hpp`：

```cpp
class Transform {
public:
    Vec3f  localPosition{0, 0, 0};
    Quatf  localRotation = Quatf::identity();
    Vec3f  localScale{1, 1, 1};

    const Mat4f &worldMatrix() const;
    void         setDirty();

private:
    mutable Mat4f m_worldMatrix = Mat4f::identity();
    mutable bool  m_dirty = true;
    Transform*    m_parent = nullptr;
};
```

- `worldMatrix()` 按父链懒计算
- `setDirty()` 向下传播

### REQ-202 · Scene 节点层级

- `SceneNode` 持有 `Transform` + `std::vector<SceneNodeSharedPtr> children`
- `Scene::root()` 返回根节点
- 插入 / 移除 / 按 path 查找（`world/player/arm`）

**验收**：构造 `world → player → arm → weapon` 层级；移动 player 后 weapon 世界坐标自动跟随。

### REQ-203 · Camera 接入 Transform

- `Camera` 改用 `Transform`（取代 `position / target / up` 裸字段）
- Orbit / FreeFly 控制器改写 Transform 字段

**验收**：相机作为场景节点，可挂到玩家节点下实现“跟随”。

### REQ-204 · Gamepad 输入

- `IInputState` 扩展 gamepad 轴 / 按钮
- SDL3 实现加 `SDL_Gamepad` 映射

**验收**：手柄左摇杆驱动 FreeFly。

### REQ-205 · Action Mapping

- `ActionMap` 把 “device event” 映射到 “action name”
- 默认配置通过 YAML / JSON 加载（Phase 3 完整序列化前可硬编码）

```yaml
actions:
  move_forward: [key:W, gamepad:leftStick.y+]
  fire:         [mouse:left, gamepad:RT]
```

- Script / gameplay 只查 `action("fire").pressed()`，不直接查设备

**验收**：同一动作绑到 3 种设备，游戏逻辑不变。

### REQ-206 · Time 模块扩展

- Fixed step 累加器（物理用）
- 暂停 / 时间缩放（0.0 / 0.5 / 2.0）
- 帧率 cap
- 与 `EngineLoop::tickFrame` 协作：`variable update` + `fixed update * N`

**验收**：暂停后 `Clock::tick()` 累进但 deltaTime 为 0；2× 速度下 deltaTime 翻倍。

### REQ-207 · `dumpScene()` 结构化输出

```cpp
std::string Scene::dump(DumpFormat format, DumpResolution res) const;
```

三档分辨率契合 [P-7](principles.md#p-7-多分辨率观察)：

- `Summary`（1–3 行）："Scene: 127 nodes, 3 cameras, 12 lights"
- `Outline`（层级 + 类型）
- `Full`（所有字段 + 矩阵）

格式至少支持 `Tree`（人类可读） + `Json`（机器可读）。

**验收**：打印一个 5 节点场景的 tree 形式。

### REQ-208 · `describe(entity)` 族 API

- `describeCamera(name)`
- `describeLight(name)`
- `describeMaterial(name)`
- `describePipeline(key)`

每个返回结构化文本 + drill-down hint（[P-7](principles.md#p-7-多分辨率观察)）。

**验收**：在 `scene_viewer` 的 ImGui 面板增加一个 `Describe` 按钮，点击输出当前相机的 JSON 到日志。

### REQ-209 · AABB + 空间索引

- `Mesh::boundingBox`（加载时计算；Phase 1 REQ-110 共用）
- `SceneNode::worldAABB()` 由 world matrix × local AABB 得到
- 简单空间索引（KD-tree 或 bucket grid），支持 `scene.queryBox(bounds)` / `scene.queryRay(origin, dir)`

**验收**：点选场景中某位置，返回命中节点列表。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M2.1 · Transform + 层级 | REQ-201 + REQ-202 + REQ-203 | 父子节点随动 |
| M2.2 · 扩展输入 | REQ-204 + REQ-205 | 手柄驱动 FreeFly；action 映射可切换 |
| M2.3 · 扩展时间 | REQ-206 | 暂停 / 时间缩放 / 固定步长 |
| M2.4 · 文本内省 | REQ-207 + REQ-208 | `dump-scene` CLI 打印 tree / JSON |
| M2.5 · 空间索引 | REQ-209 | 射线点选返回命中节点 |

## 风险 / 未知

- **Transform dirty 传播成本**：深层树每帧全量重算过慢。解决：按需重算 + dirty 缓存。
- **文本内省的字段稳定性**：LLM 依赖输出字段名。需要在 P-15 框架下加 `schema_version`。
- **空间索引选型**：KD-tree 静态场景好；bucket grid 对动态物体友好。先做简单 bucket grid。
- **Action mapping 与脚本层耦合**：Phase 6 TS 绑定时需暴露 action ID 到 TS 类型。

## 与 AI-Native 原则契合

- [P-1 确定性](principles.md#p-1-确定性是架构级不变量)：`Clock` 决定推进节奏；fixed step 是物理可重放的基础。
- [P-5 语义查询](principles.md#p-5-语义查询层)：空间索引 + `dumpScene` 是语义查询的数据源。
- [P-7 多分辨率](principles.md#p-7-多分辨率观察)：`dumpScene` 的 Summary / Outline / Full 三档是本原则的直接落地。
- [P-16 多模态](principles.md#p-16-文本优先--文本唯一)：文本内省是 agent 的主要感官。
- [P-19 命令总线](principles.md#p-19-双向命令总线)：Transform / scene 修改都应走命令层，本阶段预留接入点。

## 与现有架构契合

- `Scene` 当前已是 renderable 容器，加父子仅是结构扩展。
- `SceneNode` 的 validated cache 不受层级影响（它只看 mesh / material）。
- `Camera::cullingMask` + `SceneNode::visibilityLayerMask` 已落地，本阶段不动可见性过滤。
- ImGui overlay 可复用作“describe 面板”的展示层。

## 下一步

本阶段与 Phase 1 并行；两者之一完成后进入 [Phase 3](phase-3-asset-pipeline.md)。`dumpScene` 与 `describe` 是 Phase 10 MCP tool 的数据源，提前落地可给 agent demo 铺路。
