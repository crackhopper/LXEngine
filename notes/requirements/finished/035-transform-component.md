# REQ-035: Transform 组件 — 把 SceneNode 的 local 矩阵升级为可分解的 TRS 值类型

> 本 REQ 是 [Phase 1.5 ImGui Editor MVP + 命令总线](../../roadmaps/main-roadmap/phase-1.5-imgui-editor-mvp.md) 的第 1 步（编辑器基础前置）。在 roadmap 中以"REQ-201 Transform 组件"前向声明，本文件是它的实质化。

## 背景

`src/core/scene/object.hpp:88-178` 中 `SceneNode` 已有完整 parent/child + 世界变换 lazy 计算 + dirty 传播能力，但本地变换以 `Mat4f m_localTransform` 形式直接存储。这套现状有三个具体问题：

1. **不可分解**：当编辑器需要在 inspector 里展示 / 修改"位置 X / 旋转 RY / 缩放 1.0"这种 TRS 字段时，必须从 `Mat4f` 反推欧拉角 / 缩放，而 mat4 → TRS 的反推不唯一（剪切、负缩放、欧拉万向锁），数值会随每次 round-trip 漂移。
2. **没有 quaternion 表示**：旋转用矩阵存储，gizmo 旋转交互（拖拽 ImGuizmo 旋转环）会产生欧拉积累误差；TRS 命令（如 `rotate <node> 0 90 0`）也无法稳定地表达"绕 Y 轴 90°"。
3. **对接命令总线不友好**：编辑器命令 `move <node> 1 0 0` / `rotate <node> 0 90 0` / `scale <node> 2 2 2` 都希望直接读写独立的 t / r / s 三段，而不是把 mat4 拆开。

[REQ-041 ImGui Editor MVP](../041-imgui-editor-mvp.md) 与 [REQ-040 Editor 命令总线](../040-editor-command-bus.md) 都依赖一个稳定可分解的 transform 值类型。

## 目标

1. 引入值类型 `Transform { Vec3 translation, Quat rotation, Vec3 scale }`，作为 `SceneNode::m_localTransform` 的新表达
2. 保留 `SceneNode` 现有的 lazy 世界变换计算 + 子节点 dirty 传播逻辑，仅替换 local 表达
3. 提供 `Transform::toMat4()` 与 `Transform::fromMat4(const Mat4f&)` 作为迁移桥接，但 `SceneNode` 公共 API 以 `Transform` / TRS setter 为准，不保留长期的 `Mat4f` setter 兼容层
4. 不引入新依赖；旋转复用仓库现有 `Quat` 类型（若不存在则在 `src/core/math/` 新增最小实现）

## 需求

### R1: 新增 `Transform` 值类型

`src/core/math/transform.hpp`（新文件）：

```cpp
struct Transform {
  Vec3f translation{0.0f, 0.0f, 0.0f};
  Quatf rotation{1.0f, 0.0f, 0.0f, 0.0f};   // identity (w=1, xyz=0)
  Vec3f scale{1.0f, 1.0f, 1.0f};

  Mat4f toMat4() const;
  static Transform fromMat4(const Mat4f &m);    // 用于一次性从旧数据迁移

  static Transform identity() { return Transform{}; }
};
```

- 默认构造为 identity
- 复合操作（`Transform::operator*`）按 `T_parent * T_child` 语义实现，等价于矩阵乘
- 不引入欧拉角字段；inspector 渲染用临时局部欧拉只在 UI 层做，不进入数据模型

### R2: `Quat` 最小实现（若仓库尚无）

如果 `src/core/math/` 目前缺少 quaternion：

- 提供 `Quatf{w, x, y, z}` 值类型
- 提供 `Quatf::fromAxisAngle(Vec3f axis, float radians)`
- 提供 `Quatf::fromEulerXYZ(float rx, float ry, float rz)`
- 提供 `Quatf::operator*(Quatf)` 与 `Quatf::rotate(Vec3f)`
- 提供 `Quatf::toMat3()` / `toMat4()`
- 不实现球面插值 SLERP（v2 再加，本 REQ 不需要）

如果仓库已有 quaternion，本 R 跳过，仅在 transform.hpp 中 include 已有头。

### R3: `SceneNode` 内部存储改为 `Transform`

修改 `src/core/scene/object.hpp` / `object.cpp`：

- `m_localTransform` 类型从 `Mat4f` 改为 `Transform`
- 保留 `m_worldTransform`（仍是 `Mat4f`，由 `m_localTransform.toMat4()` 与 parent 链合成）
- 保留 `m_worldTransformDirty` 与 `markChildrenDirty()` 机制不变
- `getWorldTransform()` 调用路径不变（仍返回 `Mat4f`）
- `updateWorldTransformIfNeeded()` 内部把 `m_localTransform` 转成 mat4 后再合成

### R4: 新增公共 setter / getter

```cpp
class SceneNode {
 public:
  const Transform& getLocalTransform() const;
  void setLocalTransform(const Transform &t);

  void setTranslation(const Vec3f &v);
  void setRotation(const Quatf &q);
  void setScale(const Vec3f &s);

  Vec3f getTranslation() const;
  Quatf getRotation() const;
  Vec3f getScale() const;
};
```

每个 setter 触发 `markWorldTransformDirty()`。

### R5: 不保留 `setLocalTransform(const Mat4f&)` 兼容重载

- `SceneNode` 公共接口只保留：
  - `setLocalTransform(const Transform&)`
  - `setTranslation(...)`
  - `setRotation(...)`
  - `setScale(...)`
- 删除 `setLocalTransform(const Mat4f&)` 重载，避免把"矩阵直写"继续固化为长期 API
- 现有所有调用 `setLocalTransform(mat4)` 的代码（demo / test / scene 构造）在本 REQ 内一并改成：
  - 纯平移场景：直接改 `setTranslation(...)`
  - 完整 TRS 场景：构造 `Transform{translation, rotation, scale}` 后传入
  - 只有历史矩阵常量且暂时不值得手拆的场景：调用方显式写 `Transform::fromMat4(m)`，但这是调用点自己的迁移代码，不是 `SceneNode` 再提供隐式兼容层
- 本 REQ 接受一次性重构调用点，不接受保留"过渡 API"

### R6: `fromMat4` 不可逆时的语义

`Transform::fromMat4(m)` 在矩阵含剪切 / 负缩放等不可分解情况下：

- 提取 translation 为最后一列前三分量
- 用极分解 (polar decomposition) 提取 rotation + scale；忽略剪切
- 负缩放：保留 X 上的负号；Y/Z 翻成正号 + 在 quaternion 上乘 180°（这是常见约定，与 GLM 一致）
- 当检测到"不是严格 TRS"的输入（例如剪切、负缩放修正、奇异/不可逆 3x3 基底）时，必须输出 `WARN` 日志，明确这是一次"近似重构"
- 文档明确"重构出的 Transform 经 toMat4 后**不一定**严格等于输入"，仅保证视觉合理；`WARN` 的目的就是阻止调用方误以为这是无损 round-trip

### R7: 不引入新存储字段冗余

- `SceneNode` 不同时持有 `Transform` + `Mat4f localTransform`
- world 矩阵保持 `Mat4f` 缓存（编辑器 / pipeline 都直接消费 mat4）
- 父节点链合成时按需把 child.local 转成 mat4 — 这是热路径，但在 dirty 传播下每帧只算一次，性能可接受

## 测试

- `Transform::identity().toMat4()` == `Mat4f::identity()`
- `Transform::fromMat4(Mat4f::translate({1,2,3}))` 的 translation == `{1,2,3}`、rotation == identity、scale == `{1,1,1}`
- `Transform::fromMat4(t.toMat4())` 与 `t` 在 translation / rotation / scale 三个字段上数值近似（允许 1e-5 误差），前提是 `t` 不含负缩放或剪切
- `setTranslation(...)` 触发 dirty；后续 `getWorldTransform()` 返回更新后的世界矩阵
- 父节点 `setRotation(...)` 后，所有子节点的 `getWorldTransform()` 立即反映新世界矩阵（dirty 传播链工作）
- 原先所有 `setLocalTransform(Mat4f::...)` 调用点迁移后仍通过既有 demo / test / scene 构造路径
- 对含剪切或负缩放修正的 `Transform::fromMat4(...)` 输入，测试应断言会产生 `WARN` 日志

## 修改范围

- `src/core/math/transform.hpp` / `.cpp`（新）
- `src/core/math/quat.hpp` / `.cpp`（新，若仓库尚无）
- `src/core/scene/object.hpp` / `.cpp`（字段类型 + setter / getter + dirty 传播保留）
- `src/core/scene/scene.cpp`（如有直接构造 SceneNode 并赋 mat4 的位置，改用新 API）
- `src/test/integration/`（新增 transform 数学测试 + scene_node TRS 测试）
- `notes/source_analysis/src/core/scene/object.md`（落地后更新）

## 边界与约束

- 本 REQ **不**改变 dirty 传播机制；仅替换 local 表达
- 本 REQ **不**引入 quaternion SLERP / Squad / Catmull-Rom（动画用，留 Phase 4）
- 本 REQ **不**引入 transform 钩子 / 监听器（编辑器命令直接调 setter；监听由 [REQ-040 命令总线](040-editor-command-bus.md) 在命令层处理）
- 本 REQ **不**引入"分离 transform / TRS 模式"开关；统一用 `Transform`，需要历史矩阵迁移时由调用方显式写 `Transform::fromMat4(...)`
- world transform 缓存仍是 `Mat4f`，不是 `Transform` —— 世界级 TRS 分解涉及非交换 scale 与 shear，不安全；编辑器 inspector 显示的总是 local TRS

## 依赖

- 现有 `SceneNode` parent/child + dirty 传播（`src/core/scene/object.hpp:122-176`）
- 现有 `Vec3f` / `Mat4f`（`src/core/math/`）
- 若仓库尚无 `Quatf`：本 REQ 在 R2 内一并提供

## 后续工作

- [REQ-036 场景节点路径查询](036-scene-node-path-lookup.md) — 路径解析时复用 SceneNode 的 transform setter
- [REQ-037 Camera 作为 SceneNode](../037-camera-as-scene-node.md) — Camera 直接消费 `SceneNode::getLocalTransform()`
- [REQ-040 Editor 命令总线](../040-editor-command-bus.md) — `move / rotate / scale` 命令直接调 R4 三个 setter
- [REQ-041 ImGui Editor MVP](../041-imgui-editor-mvp.md) — inspector 把 `Transform` 渲染成 3 行 drag float

## 实施状态

2026-05-01 已落地并验证通过。作为 Phase 1.5 第 1 步，为后续编辑器 / 命令总线相关 REQ 提供字段层前置。

- 新增 `src/core/math/transform.{hpp,cpp}`，提供 `Transform` 值类型、`toMat4()`、`fromMat4()`、`operator*`
- `src/core/scene/object.{hpp,cpp}` 改为 `Transform m_localTransform`，保留 world 矩阵缓存与 dirty 传播
- `SceneNode` 公共 API 删除 `setLocalTransform(const Mat4f&)`，改为 `setLocalTransform(const Transform&)` 与 `setTranslation/Rotation/Scale`
- `Transform::fromMat4(...)` 对 shear / negative-scale repair / singular basis fallback 输出 `WARN`
- `src/test/integration/test_math.cpp` 新增 transform round-trip 与 `WARN` 回归测试
- `src/test/integration/test_scene_node_validation.cpp` 迁移原矩阵 setter 调用点到 TRS setter
- `notes/subsystems/scene.md` 已同步到 `Transform` 语义
- 验证通过：
  - `cmake .. -G Ninja`
  - `ninja test_math test_scene_node_validation`
  - `./src/test/test_math`
  - `./src/test/test_scene_node_validation`
  - `ninja BuildTest`
  - `ctest --output-on-failure -R "test_math|test_scene_node_validation"`

说明：修改范围中原写的 `notes/source_analysis/src/core/scene/object.md` 在当前仓库不存在，本次以 `notes/subsystems/scene.md` 作为对应的人类说明文档更新落点。
