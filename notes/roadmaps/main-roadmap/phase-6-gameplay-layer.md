# Phase 6 · Gameplay 层（TypeScript）

> **目标**：让**非引擎代码**（游戏逻辑）能被干净地写出来，并且**对 AI 友好**。组件生命周期、事件总线、**TypeScript 脚本**。
>
> **依赖**：Phase 2（SceneNode / Transform / Clock / 文本内省）、Phase 3（资产管线）、Phase 4（动画）、Phase 5（物理）。
>
> **可交付**：`demo_first_game` — 一个有分数、玩家输入、胜利条件、至少 2 种组件的最小可玩原型。核心游戏逻辑用 TypeScript 写。

## 为什么是 TypeScript 而不是 Lua / 自研 DSL

原 roadmap 草案里这一层推荐 Lua。AI-Native 版本改为 TypeScript，原因：

| 维度 | Lua | 自研 DSL | TypeScript |
|------|-----|--------|-----------|
| LLM 掌控力 | 中（小众） | 极低 | 极强（前端主力语言） |
| 训练语料密度 | 低 | 零 | 极高 |
| 静态类型 | 无 | 视设计 | 有 —— LLM 生成出错率显著降低 |
| 与 UI 层统一 | 无法 | 无法 | 与 Phase 8 的 UI 容器共用同一语言和工具链 |
| 工具链 | 成熟 | 自己造 | 成熟 |
| 嵌入成本 | 低 | — | 中（嵌入式 JS 引擎 ~百 KB 量级） |
| 热重载 | 容易 | 视设计 | 容易 |

**代价**：嵌入 JS 引擎比嵌入 Lua 大一个数量级。但对"AI 生成代码 → 塞进引擎跑"的目标，TS 的掌控力收益压倒嵌入成本。

**选型参考**：在嵌入式 JS 引擎生态里选一个小型、CMake 友好、C++ 友好、支持现代 ES 的实现即可。具体产品不在本 roadmap 冻结。

## 范围与边界

**做**：
- 组件模型（组件挂载到 SceneNode，有完整生命周期）
- 系统调度（按组件类型分发更新）
- 类型元数据（组件能被序列化 / 编辑器识别，不需要手写 switch）
- 事件系统（typed pub/sub）
- Coroutine / 延迟执行
- **TypeScript 脚本运行时**（嵌入式 JS 引擎 + 预编译或运行期 ts→js 编译）
- **引擎 → TS 绑定自动生成**（复用 Phase 2 / Phase 4 的反射元数据，走 P-4 Capability Manifest 单源派生）
- 原型对象（prefab）
- 实例化 / 销毁（spawn / despawn）

**不做**：
- ECS（entity-component-system 架构）—— 先走更简单的 component-on-node 模型，后续如需再迁移
- 序列化版本迁移
- 网络同步
- 保存游戏状态（save / load game）—— 走 Phase 3 的 scene serialization 能覆盖
- V8 集成 —— 太大（~40MB），对小型引擎不值

---

## 前置条件

- Phase 2：`SceneNode` 有一个稳定的"可挂载点"
- Phase 3：组件能被反序列化
- Phase 4/5：组件能访问动画 / 物理 API

---

## 工作分解

### REQ-601 · Component 基类

```cpp
class Component {
public:
    virtual ~Component() = default;

    virtual void onAwake()        {}  // 构造后、激活前一次
    virtual void onStart()        {}  // 激活后、第一次 update 前一次
    virtual void onUpdate(float)  {}  // 每帧
    virtual void onFixedUpdate(float) {}  // 每物理步
    virtual void onLateUpdate(float)  {}  // 每帧，在所有 update 之后
    virtual void onDestroy()      {}

    SceneNode* node() const { return m_node; }
    Engine*    engine() const { return m_engine; }

private:
    friend class SceneNode;
    SceneNode* m_node = nullptr;
    Engine*    m_engine = nullptr;
    bool       m_enabled = true;
};

using ComponentPtr = std::shared_ptr<Component>;
```

- `SceneNode` 持有 `std::vector<ComponentPtr> components`
- `SceneNode::addComponent<T>(args...)` 工厂
- `SceneNode::getComponent<T>() -> T*`

**验收**：`test_component_lifecycle.cpp`：生命周期调用顺序正确，销毁不泄漏。

### REQ-602 · System 调度

简单方案：`Engine` 维护一个 `std::vector<SceneNodePtr>`，每帧遍历所有节点调所有组件的 `onUpdate`。

更干净的方案：按组件类型分 bucket：

```cpp
class ComponentRegistry {
public:
    template <typename T>
    void registerType();  // 登记 T 的元数据 + bucket

    template <typename T>
    std::vector<T*>& getAll();  // 所有活着的 T

    void dispatch(std::function<void(Component*)> fn);
};
```

- Engine 的更新顺序：
  1. 新加组件 `onAwake` → `onStart`
  2. 所有 `onUpdate(dt)`
  3. 物理 `fixedUpdate`（外加 accumulator）
  4. 所有 `onLateUpdate(dt)`
  5. 销毁队列 `onDestroy`

**验收**：多种组件混合，执行顺序正确。

### REQ-603 · 类型元数据 / 反射

组件的序列化和编辑器都需要知道"这个类有哪些字段、类型是什么"。不自己写 reflection，用**宏 + 模板注册**。

```cpp
class PlayerHealth : public Component {
    LX_COMPONENT(PlayerHealth)
    LX_FIELD(int,   maxHealth, 100)
    LX_FIELD(int,   currentHealth, 100)
    LX_FIELD(float, regenRate, 1.0f)

    void onUpdate(float dt) override { /*...*/ }
};
```

`LX_FIELD` 宏实际上：
- 声明成员变量
- 注册到 `PlayerHealth` 的 `TypeInfo`（静态 map：name → offset + typeid + default）

`LX_COMPONENT` 宏：
- 声明静态 `TypeInfo`
- 注册到全局 component registry

序列化 / 反序列化 / 编辑器 inspector 全部基于 `TypeInfo` 反射，**没有一行手写 switch**。

**验收**：`test_component_reflection.cpp`：创建 `PlayerHealth` → 用 `TypeInfo` 枚举字段 → 改值 → 序列化 → 反序列化回原值。

### REQ-604 · 事件总线

```cpp
class EventBus {
public:
    template <typename E>
    void subscribe(std::function<void(const E&)> handler);

    template <typename E>
    void unsubscribe(HandlerId id);

    template <typename E>
    void publish(const E& event);
};
```

- 事件类型是任意 struct，不需要继承基类
- 用 `typeid(E).hash_code()` 做 key
- 线程不安全（先单线程）
- 组件里典型用法：`engine()->events().subscribe<DamageEvent>([this](auto&){...})`

**验收**：`test_event_bus.cpp`：订阅 → 发布 → 取消订阅 → 再发布不触发。

### REQ-605 · Coroutine / 延迟执行

- 基于 C++20 `std::coroutine`
- 提供 `co_await WaitSeconds(1.5f)` / `co_await WaitUntil(pred)` / `co_await WaitNextFrame`
- Engine 维护一个协程调度器，每帧唤醒应 resume 的

```cpp
Task<void> PlayerDeath() {
    playAnim("die");
    co_await WaitSeconds(2.0f);
    respawn();
}
```

**验收**：`test_coroutine.cpp`：`WaitSeconds` 的等待时长与 `Clock` 一致。

### REQ-606 · TypeScript 运行时

选一个**嵌入式 JavaScript 引擎**（标准 ECMAScript 支持、C++ 友好、体量可接受）接入：

- `src/core/scripting/script_runtime.hpp` 定义接口：runtime 创建 / 模块加载 / 函数调用 / 异常转换
- `src/infra/scripting/<vendor>_runtime.{hpp,cpp}` 是具体实现
- 一个引擎进程一个全局 runtime，每个脚本模块一个 context
- 脚本源文件是 `.ts`，构建时走 REQ-611 编译链；开发期可以跑预编译后的 `.js`
- 脚本错误通过 Phase 2 REQ-217 的结构化错误对象返回（带文件/行号）
- 热重载复用 Phase 3 的事件机制

**选型参考**：市场上有多个成熟的嵌入式 JS 引擎，评估维度 —— 体量、启动速度、ES 版本支持、GC 行为、C++ 绑定难度、License。

**验收**：加载一个 `hello.ts` 脚本，调用其 `greet()` 函数返回字符串给 C++。

### REQ-607 · Engine → TypeScript Bindings

契合 [P-4 Capability Manifest](principles.md#p-4-单源能力清单capability-manifest)：

需要把 C++ 侧的引擎能力暴露成 JS 全局对象 / 模块。一个 binding 由三部分组成：

- **native wrapper**：把脚本侧的值转成 C++ 类型并调用 C++ 函数
- **TypeScript `.d.ts`**：让 LLM / 编辑器 / 类型检查器知道接口
- **（可选）runtime shim**：纯 TS 实现的便利函数

**单源原则**：不允许 native wrapper、`.d.ts`、文档三份手写。它们全部从 Phase 2 REQ-213 的命令声明 + REQ-603 的 TypeInfo 一份源头派生。新增一条命令 → 重跑 binding 生成 → TS 一端自动感知。

首批绑定：

```typescript
// engine.d.ts  (由工具生成)
declare const engine: {
    scene: {
        findNode(path: string): SceneNode | null;
        createNode(parent: SceneNode, name: string): SceneNode;
        destroyNode(node: SceneNode): void;
    };
    input: {
        isKeyDown(key: string): boolean;
        mousePosition(): [number, number];
        action(name: string): number;
    };
    time: {
        deltaTime(): number;
        totalTime(): number;
    };
    events: {
        on<T>(name: string, handler: (e: T) => void): void;
        emit<T>(name: string, event: T): void;
    };
    log: {
        info(msg: string): void;
        warn(msg: string): void;
        error(msg: string): void;
    };
};

declare class SceneNode {
    readonly name: string;
    position: [number, number, number];
    rotation: [number, number, number, number];  // quaternion
    scale:    [number, number, number];
    getComponent<T>(type: string): T | null;
    addComponent<T>(type: string, init?: Partial<T>): T;
}
```

**验收**：一个纯 TS 写的 `spinOnY.ts` 能让挂载的物体以给定速度旋转。

### REQ-611 · TS → JS 编译与打包

- 开发期：用一个**快速的 TS → JS 编译器**跑单文件编译（毫秒级）
- 构建期：构建系统的自定义 target 扫描 `scripts/` 下所有 `.ts`，产出对应 `.js`
- 运行期：引擎从 `.js` 加载，不携带编译器
- Sourcemap：保留，错误栈能定位到 `.ts` 行号

**选型参考**：现代 TS/JS 生态里有多个支持 Go/Rust 实现的快速编译器（启动和编译都很快），任选。

**验收**：`scripts/player.ts` 保存后刷新客户端能看到新行为，错误栈指向原始 `.ts` 行号。

### REQ-612 · Script 组件

```cpp
class ScriptComponent : public Component {
    LX_COMPONENT(ScriptComponent)
    LX_FIELD(std::string, scriptPath, "")   // "scripts/player.js" 之类

    void onAwake() override;    // 加载 JS 模块 + 实例化 class
    void onStart() override;    // 调 JS 的 onStart
    void onUpdate(float dt) override;  // 调 JS 的 onUpdate
    void onDestroy() override;  // 调 JS 的 onDestroy + 释放引用

private:
    JSValue m_instance;
};
```

- 一个 TS 脚本等价于一个 `Component` 子类，TS 侧的 class 继承 `BaseScript` 基类
- `BaseScript` 提供 `this.node`、`this.engine`、生命周期钩子
- `ScriptComponent` 在反射 TypeInfo 里声明 `scriptPath` 字段，让编辑器能绑脚本

**验收**：把 `spinOnY.ts` 通过 `ScriptComponent(scriptPath="scripts/spinOnY.js")` 挂到一个节点上，旋转生效。

### REQ-607 · Prefab 系统

- **Prefab** 是序列化好的 SceneNode 子树模板
- 文件存在 `assets/prefabs/*.prefab`（JSON 格式，Phase 3 序列化的子集）
- `Engine::instantiate(prefabHandle, parent)` 反序列化并加到指定 parent 下
- 运行期 spawn 走这条路径

**验收**：`demo_first_game` 里子弹 / 敌人全部是 prefab 实例化出来的。

### REQ-608 · Spawn / Despawn

- `SceneNode::destroy()` 不立即删，只标记 `m_markedForDestruction`
- Engine 在每帧末尾统一扫描 + 实际销毁
- 对应地，新建也在"下一帧开始"激活（`onAwake` → `onStart`）
- 这样避免遍历组件列表时被修改

**验收**：组件 `onUpdate` 里安全调用 `node()->destroy()` 不导致迭代器失效。

### REQ-609 · 第一个游戏原型

选一个规模合适的范例：**marble maze**（最简单）或 **twin-stick shooter**（更完整）。

要用到的组件（至少）：
- `PlayerInputComponent`（读 InputState → 调 CharacterController 或施加力）
- `EnemyAI`（简单的 "朝玩家走" 逻辑）
- `ScoreTracker`（全局单例组件，监听 DamageEvent）
- `HealthComponent`（被 Phase 7 UI 读）
- `GoalTrigger`（物理 trigger → 游戏胜利事件）

**验收**：`demo_first_game` 可以从开始到胜利 / 失败，不崩溃。

---

## 里程碑

### M6.1 · 组件模型成形

- REQ-601 + REQ-602 + REQ-603 完成
- demo：组件生命周期走完，字段能被反射读写

### M6.2 · 事件 + 协程

- REQ-604 + REQ-605 完成
- demo：组件用事件解耦 + 协程控制序列动作

### M6.3 · Prefab + TypeScript 脚本

- REQ-606 + REQ-607 + REQ-611 + REQ-612 + REQ-607(prefab) + REQ-608 完成
- demo：纯 TypeScript 写的玩家逻辑通过 prefab 实例化

### M6.4 · 可玩原型

- REQ-609 完成
- demo：`demo_first_game`，一个可玩的小游戏

---

## 风险 / 未知

- **组件 vs ECS 的选型后悔**：一旦选定 component-on-node，迁移到 data-oriented ECS 成本高。但对一个**小型**引擎来说，大量组件 cache miss 不是瓶颈；选 component-on-node 开发效率优先。
- **反射宏的编译时间**：反射相关宏展开会产生较多模板实例化。用 constexpr + 固定大小数组控制膨胀。
- **嵌入式 JS 引擎的性能**：比 V8 这类 JIT 引擎慢 10–100 倍，但对游戏逻辑层足够。热点代码仍然写在 C++。
- **TS binding 自动生成的覆盖率**：复杂模板类型（nested generic）难以从 C++ 反射直接出 TS。对复杂类型手写 `.d.ts` override，和自动生成合并。
- **Sourcemap 对错误调试的重要性**：JS 报错能不能定位回 `.ts` 行号，决定开发体验。必做。
- **协程 + 组件销毁的生命周期交织**：组件被销毁时如果协程还在跑，资源会悬垂。规则：协程内捕获弱引用，每次 resume 前检查。
- **事件总线的性能**：全局类型分发 + 函数对象调用有开销。对 100 Hz 级别的常见事件没问题；高频（每帧每物体）事件避开它，用直接回调。

---

## 与现有架构的契合

- `SceneNode` 的设计（Phase 2）从一开始就预留"组件挂载点"，Phase 6 不是破坏性改造而是填空。
- `StringID` 给组件类型名、事件名、prefab 名提供 O(1) 比较。
- Phase 3 的资产 handle 让 prefab 可以用强类型句柄。
- Phase 4 / Phase 5 的 `AnimationPlayer` / `RigidBody` 本质上就是"特殊组件"，本阶段回过头来把它们统一成 `Component` 子类。
- `Engine::onUpdate / onFixedUpdate` 的回调钩子（Phase 2）升级为"遍历所有组件并调"的实现。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-1 确定性](principles.md#p-1-确定性是架构级不变量) | 组件 update 顺序由类型 registry 决定 + 脚本不允许裸随机源 |
| [P-3 三层 API](principles.md#p-3-三层-api查询--命令--原语) | 脚本**只能**通过命令层修改状态，不能直接改组件字段 |
| [P-4 Capability Manifest](principles.md#p-4-单源能力清单capability-manifest) | TS 绑定 / MCP tool / 编辑器 UI 从 TypeInfo 单源派生 |
| [P-6 Intent Graph](principles.md#p-6-意图图intent-graph) | 脚本通过命令层写入时自动进入 intent tracking |
| [P-18 沙箱进程](principles.md#p-18-沙箱友好的进程模型) | 脚本 runtime 没有文件系统 / 网络直接访问，全部走 engine API |

---

## 下一步

Gameplay 层完成后，游戏已经能跑。剩下的是"让它好玩 + 有声音 + 有 UI" → [Phase 7 音频](phase-7-audio.md) + [Phase 8 Vue UI](phase-8-web-ui.md)。
