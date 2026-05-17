# AI 扫描总览：当前代码里的高风险缺口

这次扫描覆盖了 `src/core/`、`src/infra/`、`src/backend/` 以及对应的构建入口，目标不是列 API，而是找出会影响正确性、可维护性和后续演进速度的问题。我们优先记录已经能从代码中直接证实的问题，再补充少量结构性债务。

## 本轮结论

| 层 | 重点问题 | 严重度 |
| --- | --- | --- |
| Core | 四元数乘法公式写错；向量哈希存在未定义行为；Scene 与 SceneNode 用裸指针回连 | 高 |
| Infra | SDL 窗口 resize 接口被空实现覆盖；`TextureLoader` 重复加载泄漏；ImGui 包装未接线且一旦启用会断言 | 高 |
| Backend | GLFW Surface 句柄契约错误；物理设备筛选把集显全部排除；队列族探测复用成员状态 | 严重 |

## 阅读顺序

1. [Backend Vulkan 扫描](backend-vulkan.md)
2. [Infra 与构建层扫描](infra-and-build.md)
3. [Core 与场景层扫描](core-and-scene.md)

## 范围与方法

| 项 | 说明 |
| --- | --- |
| 扫描日期 | 2026-04-17 |
| 方法 | 代码静态阅读、跨文件引用检查、构建脚本检查、约束规范对照 |
| 未做的事 | 没有逐个跑全量 GPU 集成测试；没有把第三方依赖目录当成问题来源 |

## 继续阅读

- [C++ Style Guide](../../openspec/specs/cpp-style-guide/spec.md)
- [Architecture](../concepts-design/architecture.md)
- [VulkanBackend](../subsystems/vulkan-backend.md)
