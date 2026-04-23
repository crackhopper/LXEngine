# 技术预研 (research/)

> 目录名：`research/`（原 `核心技术演进调研/`，已改为英文以对齐其他路径规范）。
>
> 本目录用于记录未来可能引入、但**当前阶段不实施**的核心渲染/引擎技术调研。与 `phase-*` 阶段文档不同，这里的内容**不进入路线图时间线**，仅作为技术储备与参考。

## 用途

- 记录业界主流引擎的做法与演进方向
- 为 LX Engine 架构选型提供事前参考，避免日后重复调研
- 标注"何时考虑引入"的触发条件，而不是"什么时候一定要做"

## 当前收录

| 主题 | 文件 | 简述 | 调研日期 |
|------|------|------|---------|
| Bindless Texture | [bindless-texture/README.md](bindless-texture/README.md) | bindless 资源绑定在现代引擎的采用情况、对 pipeline 数量的影响、ubershader/permutation 策略、nonuniform 深入、业界对比、LX 演进路径（6 篇 + 入口索引） | 2026-04-23 |
| Pipeline Cache | [pipeline-cache/README.md](pipeline-cache/README.md) | 应用层对象缓存 vs 驱动层 `VkPipelineCache` 两层关系、LX 当前实现、若接入底层机制的演进路径 | 2026-04-23 |
| Multi-threading | [multi-threading/README.md](multi-threading/README.md) | task-based vs fiber-based 选型、enkiTS + pinned task 异步 I/O 模式、LX 当前单线程现状、分阶段演进路径 | 2026-04-23 |

## 写入规范

新增一篇技术调研时，文件头建议包含：

```
> 状态：仅记录 / 已立项 / 已废弃
> 调研日期：YYYY-MM-DD
> 目的：一句话说明为什么记录此技术
```

正文推荐结构：

1. **业界采用情况** —— 哪些引擎在用，成熟度如何
2. **对现有架构的影响** —— 如果引入，需要改动哪些子系统
3. **替代方案对比** —— 至少列出 2 种备选路线
4. **对 LX Engine 的演进建议** —— 分步走的增量引入路径
5. **风险提示** —— 性能、兼容性、平台支持等
6. **参考资料**
