## Why

`SceneNode` 现在把 local 变换存成 `Mat4f`，这让 inspector、命令总线、gizmo 这类需要稳定 TRS 字段的路径很难落地。继续保留矩阵直写会把不可逆分解、欧拉漂移、历史调用点惯性一起固化到 Phase 1.5 后续工作里。

## What Changes

- 把 `SceneNode` 的 local 变换表达从 `Mat4f` 升级为显式 `Transform { translation, rotation, scale }` 值类型。
- 保留现有 parent/child 世界矩阵懒计算与 dirty 传播，只替换 local 存储与公共 setter/getter 形态。
- **BREAKING** 删除 `SceneNode::setLocalTransform(const Mat4f&)` 公共重载，现有 demo/test/scene 构造调用点在本变更内迁到 `Transform` / TRS setter。
- 新增 `Transform::toMat4()` / `Transform::fromMat4()` 作为迁移桥接，并定义剪切、负缩放、奇异矩阵输入下的近似重构与 `WARN` 语义。
- 扩展数学与场景层测试，覆盖 TRS round-trip、层级 dirty 传播、历史矩阵调用点迁移后的行为等回归面。

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `scene-transform-hierarchy`: local/world 变换合同从“存 Mat4f”升级为“存 Transform、导出 world Mat4f”，并移除 `SceneNode` 的矩阵直写 setter 兼容层。
- `math-correctness`: 新增 `Transform` 的矩阵往返、不可逆分解近似语义、以及告警可观测性要求。

## Impact

- Affected code:
  - `src/core/math/transform.{hpp,cpp}` 新增
  - `src/core/scene/object.{hpp,cpp}` 本地变换存储与 API 调整
  - `src/test/integration/test_math.cpp`
  - `src/test/integration/test_scene_node_validation.cpp`
  - 其他直接调用 `setLocalTransform(Mat4f)` 的 demo/test/scene setup
- API impact:
  - `SceneNode::setLocalTransform(const Mat4f&)` 删除，调用方必须迁移
  - `SceneNode::getLocalTransform()` 返回 `Transform`
- No new external dependencies.
