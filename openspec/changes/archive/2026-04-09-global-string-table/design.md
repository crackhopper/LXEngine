## Context

当前 `material.hpp` 中 `MaterialTemplate::m_bindingCache` 和 `MaterialInstance` 的属性存储均以 `StringID` 为 key，但 `StringID` 类型和 `MakeStringID` 函数尚未定义，导致代码无法编译。`RenderPassEntry::bindingCache` 仍使用 `std::string` key。

在渲染循环中，材质属性的查找和比较是高频操作。使用字符串作为 key 意味着每次查找都需要计算字符串哈希并进行字符串比较，而整数比较只需一条 CPU 指令。

## Goals / Non-Goals

**Goals:**
- 实现 `GlobalStringTable` 单例，提供线程安全的字符串驻留功能
- 定义 `StringID` 结构体，封装 `uint32_t`，支持从字符串隐式构造
- 提供 `MakeStringID` 便捷函数
- 使 `material.hpp` 中已有的 `StringID` 用法可以编译通过

**Non-Goals:**
- 不修改 `RenderPassEntry::bindingCache` 的 key 类型（它是 shader 反射的直接映射，保持 `std::string`）
- 不为 `StringID` 引入编译期常量优化（constexpr hash），当前阶段使用运行时查表即可
- 不修改 shader 反射系统或其他资源类型

## Decisions

### 1. 使用自增 ID 而非字符串哈希

**选择**: `GlobalStringTable` 内部维护一个自增计数器，每个新字符串分配下一个 `uint32_t` ID。

**替代方案**: 使用 `std::hash<std::string>` 的哈希值作为 ID。

**理由**: 自增 ID 保证零碰撞，无需碰撞处理逻辑。字符串哈希虽然省去了查表步骤，但存在碰撞风险，需要额外的碰撞检测和处理代码，增加复杂度。ID 空间为 2^32（约 40 亿），对于引擎中的属性名数量绰绰有余。

### 2. 单例模式 + 读写锁

**选择**: `GlobalStringTable` 使用 Meyer's Singleton（函数内 static 变量），内部使用 `std::shared_mutex` 实现读写锁。

**理由**: 读操作（查询已有字符串的 ID）远多于写操作（注册新字符串），读写锁允许多线程并发读取，仅在写入时独占。Meyer's Singleton 在 C++11 后保证线程安全初始化。

### 3. StringID 支持隐式构造

**选择**: `StringID` 提供从 `const char*` 和 `const std::string&` 的非 explicit 构造函数。

**替代方案**: 仅提供 explicit 构造函数，要求调用方显式构造。

**理由**: 隐式构造允许 `mat->setFloat("u_Time", 1.0f)` 这样的自然调用方式，用户无需改变现有使用习惯。`uint32_t` 构造函数标记为 `explicit`，防止意外的整数隐式转换。

### 4. 文件放置

**选择**: 新增 `src/core/utils/string_table.hpp`，header-only 实现。

**理由**: `core/utils/` 已存在工具类文件（`env.hpp`, `filesystem_tools.hpp`），放在此处符合项目结构。Header-only 避免引入额外的 .cpp 编译单元，简化构建。

## Risks / Trade-offs

- **[Risk] 单例生命周期**: 如果 `GlobalStringTable` 在 `main()` 结束后的静态析构阶段被访问，可能导致 use-after-destroy。→ **Mitigation**: 引擎的字符串注册应在 `main()` 生命周期内完成，静态析构阶段不应访问材质系统。
- **[Risk] 内存只增不减**: 注册到表中的字符串永远不会被释放。→ **Mitigation**: 引擎中的属性名数量有限（通常几百到几千个），内存占用可忽略。
- **[Trade-off] 首次查找有写锁开销**: 第一次遇到新字符串时需要获取排他锁写入。→ 通常在初始化/资源加载阶段发生，不影响渲染帧率。
