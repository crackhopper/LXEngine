## Why

Material 系统当前使用 `std::string` 作为属性查找的 key（如 `"u_BaseColor"`、`"u_Time"`），在渲染循环中频繁进行字符串哈希和比较，带来不必要的性能开销。同时，`material.hpp` 中已使用了 `StringID` 和 `MakeStringID`，但它们尚未被定义和实现，代码无法编译。我们需要引入一个全局字符串驻留（String Interning）系统，为每个唯一字符串分配一个全局唯一的整数 ID，从根本上消除哈希碰撞问题，并将属性查找从字符串比较降级为整数比较。

## What Changes

- 新增 `GlobalStringTable` 单例类（放在 `src/core/utils/` 下），实现线程安全的字符串-到-ID 双向映射，保证同一字符串全局只有一个唯一 ID
- 新增 `StringID` 结构体，封装 `uint32_t` ID，支持从字符串隐式构造，并提供 `Hash` 内嵌结构以用作 `unordered_map` 的 key
- 提供 `MakeStringID(const std::string&)` 便捷函数
- 修改 `material.hpp`：`MaterialTemplate` 和 `MaterialInstance` 中以 `StringID` 替代 `std::string` 作为绑定缓存和属性存储的 key
- `RenderPassEntry::bindingCache` 保持 `std::string` key 不变（它是 shader 反射数据的直接映射，不需要变更）

## Capabilities

### New Capabilities
- `string-interning`: 全局字符串驻留系统，提供 `GlobalStringTable`、`StringID` 结构体和 `MakeStringID` 函数，保证字符串到整数 ID 的全局唯一映射

### Modified Capabilities
<!-- 无需修改已有 spec -->

## Impact

- 新增文件：`src/core/utils/string_table.hpp`
- 修改文件：`src/core/resources/material.hpp`（添加 `#include`，使用 `StringID` 作为 key）
- 依赖：仅使用 C++ 标准库（`<shared_mutex>`, `<unordered_map>`, `<vector>`, `<atomic>`）
- 对外 API 变化：`MaterialInstance::setFloat/setVec4/setTexture` 的参数类型从隐式 `std::string` 变为 `StringID`（`StringID` 支持从 `const char*` / `std::string` 隐式构造，因此调用方代码无需修改）
