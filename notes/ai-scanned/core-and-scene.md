# Core 与场景层扫描：数学正确性和生命周期模型的裂缝

Core 层的问题不多，但有几条都比较硬。一条是数学实现本身就不对，另一条是底层工具代码用了未定义行为，还有一条是场景对象图仍然依赖裸指针回连，这和项目自己的风格规范正面冲突。

## 高风险问题

| 严重度 | 位置 | 现象 | 影响 |
| --- | --- | --- | --- |
| 高 | [src/core/math/quat.hpp](../../src/core/math/quat.hpp) | 四元数原地乘法先改写 `w`，再拿新 `w` 参与 `v` 的计算 | 旋转组合结果错误，`operator*`、`operator*=`、插值链路都会被污染 |
| 高 | [src/core/math/vec.hpp](../../src/core/math/vec.hpp) | 浮点向量哈希把 `float` 地址强转成 `long long*` 读取 | 违反严格别名和对象大小假设，存在未定义行为；不同平台/编译器下可能直接读越界 |
| 中 | [src/core/scene/object.hpp](../../src/core/scene/object.hpp) | `SceneNode` 通过 `Scene* m_scene` 和 `attachToScene(Scene*)` 回连到场景 | 生命周期靠调用约定维持，和项目“非 owning 依赖用引用、不要裸指针成员”的规范冲突 |

## 证据

### 1. 四元数乘法公式被“更新顺序”写坏了

- [src/core/math/quat.hpp](../../src/core/math/quat.hpp) 第 117-122 行：
  `w` 先被更新为 `oldW * o.w - oldV.dot(o.v)`，随后 `v` 用的是 `o.v * w`
- 正确公式应该使用旧标量分量 `oldW`，而不是已经写回的新 `w`
- `left_multiply_inplace()` 在第 124-129 行也有同样问题

这不是数值误差，而是公式级错误。只要执行四元数组合，结果就已经偏离定义。

### 2. 浮点向量哈希依赖未定义行为

- [src/core/math/vec.hpp](../../src/core/math/vec.hpp) 第 101-103 行直接把 `&v[i]` 重解释为 `const long long*`

当 `T = float` 时，这里会用 8 字节视图读取一个 4 字节对象；当对齐、优化级别或平台 ABI 变化时，结果没有语言层保证。

## 设计问题

| 类型 | 位置 | 说明 |
| --- | --- | --- |
| 规范违背 | [src/core/scene/object.hpp](../../src/core/scene/object.hpp) 第 121 行、第 141 行 | `SceneNode` 保存 `Scene*`，靠 `Scene` 析构时反向清空；这属于典型的脆弱回连 |
| 双轨实现 | [src/core/scene/object.cpp](../../src/core/scene/object.cpp) 第 90-98 行、第 382-389 行 | `SceneNode` 有完整验证缓存，`RenderableSubMesh` 仍走 legacy 路径，只做轻量包装；同一个 `IRenderable` 抽象下存在两套不同强度的约束 |
| 风格漂移 | [src/core/math/vec.hpp](../../src/core/math/vec.hpp) | Core 层本应尽量保持可移植、低惊喜实现，但底层 hash 已经为了“图方便”引入类型穿透和别名假设 |

## 建议的修正方向

| 优先级 | 建议 |
| --- | --- |
| P0 | 修正四元数乘法公式，统一使用旧值参与组合 |
| P0 | 用 `std::bit_cast` 或按元素哈希替换 `reinterpret_cast<const long long*>` |
| P1 | 重新整理 `SceneNode` 与 `Scene` 的回连方式，至少去掉裸指针成员 |
| P1 | 明确 `RenderableSubMesh` 是过渡层还是长期 API；如果继续保留，就要补齐和 `SceneNode` 一致的验证语义 |

## 继续阅读

- [Scene](../subsystems/scene.md)
- [String Interning](../source_analysis/src/core/utils/string_table.md)
- [C++ Style Guide](../../openspec/specs/cpp-style-guide/spec.md)
