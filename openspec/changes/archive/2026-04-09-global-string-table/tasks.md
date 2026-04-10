## 1. GlobalStringTable 与 StringID 实现

- [x] 1.1 创建 `src/core/utils/string_table.hpp`，实现 `GlobalStringTable` 单例类：自增 ID 分配、`getOrCreateID`、`getName`、`std::shared_mutex` 读写锁
- [x] 1.2 在同一文件中定义 `StringID` 结构体：`uint32_t id` 成员、从 `const char*` / `const std::string&` 隐式构造、`explicit uint32_t` 构造、`operator==`/`!=`、内嵌 `Hash` 结构
- [x] 1.3 在同一文件中提供 `MakeStringID(const std::string&)` 便捷函数

## 2. Material 系统集成

- [x] 2.1 修改 `src/core/resources/material.hpp`：添加 `#include "core/utils/string_table.hpp"`
- [x] 2.2 确认 `MaterialTemplate::m_bindingCache`、`buildBindingCache`、`findBinding` 使用 `StringID` 作为 key（已有代码，确保与新定义兼容）
- [x] 2.3 确认 `MaterialInstance` 的 `m_vec4s`、`m_floats`、`m_textures` 及 `setVec4`、`setFloat`、`setTexture` 使用 `StringID`（已有代码，确保与新定义兼容）

## 3. 验证

- [x] 3.1 编译验证：确保项目可以正常编译通过
