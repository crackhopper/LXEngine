# String Interning

> 所有字符串键都先变成 `StringID`。项目里不会反复拿原始字符串做比较，pipeline 身份、binding 名、pass 名都走这条路。
>
> 权威 spec: `openspec/specs/string-interning/spec.md`

## 它解决什么问题

- 把字符串比较降成整数比较。
- 给 `PipelineKey` 这类结构化身份提供统一的 compose 能力。
- 提供 `toDebugString()`，让结构化 ID 还能还原成人能读的树。

## 核心对象

- `GlobalStringTable`：全局表，负责 `Intern`、`compose`、`decompose`。
- `StringID`：`uint32_t` 包装类型，可直接做 map key。
- `TypeTag`：标记一个 compose 结果属于哪类结构。

## 典型数据流

1. 叶子名字先 `Intern("baseColor")`。
2. 各资源各自产出 `getRenderSignature(...)`。
3. `PipelineKey::build(objSig, matSig)` 再做一层 compose。
4. 调试时用 `toDebugString()` 展开。

## 使用规则

- compose 对顺序敏感，需要稳定顺序的输入必须先排序。
- 叶子概念直接 intern，不要为每个简单枚举硬造新 `TypeTag`。
- `Pass_Forward` 这类常量是 `inline const StringID`，不是 `constexpr`。

## 关联文档

- `openspec/specs/string-interning/spec.md`
- `openspec/specs/render-signature/spec.md`
- `notes/subsystems/pipeline-identity.md`
