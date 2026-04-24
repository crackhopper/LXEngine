# 模板与 Pass：材质的结构定义

## 什么是 MaterialTemplate

`MaterialTemplate` 是材质的菜谱。它不保存运行时参数值，只回答三个结构性问题：

- 这个材质支持哪些 pass（Forward? Shadow? 两者都有？）
- 每个 pass 用什么 shader、开了哪些 variants、什么 render state
- 每个 pass 的 shader 反射出了哪些材质侧 binding

一个 template 可以对应多个 instance。就像同一张菜谱可以做出很多道菜，只是每道菜的调料用量不同。

代码入口：[material_template.hpp](../../../src/core/asset/material_template.hpp)

## 什么是 MaterialPassDefinition

如果 template 是菜谱，`MaterialPassDefinition` 就是菜谱里的一个步骤。

一个渲染 pass（比如 Forward 或 Shadow）需要知道三件事：

| 字段 | 含义 | 类比 |
|------|------|------|
| `renderState` | 光栅化、深度测试、混合模式 | 这一步用什么火候 |
| `shaderSet` | shader 名 + variants + 编译后的 shader 对象 | 这一步用什么工具 |
| `bindingCache` | 这个 pass 的 shader 反射出来的所有 binding | 这一步需要哪些原料 |

每个 pass 的 shader 可以不同（比如 Shadow pass 用简化版 shader），因此它们的 `bindingCache` 也各自独立。

代码入口：[material_pass_definition.hpp](../../../src/core/asset/material_pass_definition.hpp)

## Template 如何知道哪些 binding 归材质

调用 `buildBindingCache()` 时，template 会从每个 pass 的 shader 反射中提取 binding 列表，然后用 `isSystemOwnedBinding()` 过滤：

- `CameraUBO`、`LightUBO`、`Bones` → 归系统，跳过
- 其余 → 归材质，收入该 pass 的 material-owned binding 列表

结果按 pass 分组保存在 `m_passMaterialBindings` 中。这保证了 binding 信息始终带着 pass 作用域——不同 pass 的同名 binding 不会互相覆盖。

需要跨 pass 查找 binding 时（比如 `setTexture(id, tex)` 验证类型），调用 `findMaterialBinding(id)`。它遍历所有 pass 的 material-owned bindings 返回第一个匹配项；如果同名 binding 在不同 pass 间类型不一致，会 assert。

## 在 YAML 中对应什么

用 `.material` 文件创建材质时，YAML 里的 `passes` 块直接对应 template 里的 pass 定义：

```yaml
shader: blinnphong_0                # 全局默认 shader

passes:
  Forward:                          # → template.setPass(Pass_Forward, ...)
    shader: blinnphong_0            # 可选：per-pass shader 覆盖全局
    renderState:                    # → MaterialPassDefinition.renderState
      cullMode: Back
      depthTest: true
    variants:                       # → MaterialPassDefinition.shaderSet.variants
      USE_NORMAL_MAP: true
  Shadow:                           # → template.setPass(Pass_Shadow, ...)
    shader: shadow_depth_only       # Shadow pass 可以用完全不同的 shader
    renderState:
      depthTest: true
      depthWrite: true
```

每个 pass 可以指定自己的 `shader`，覆盖顶层全局默认。这样不同 pass 可以使用完全不同的 shader 源文件。如果 YAML 里省略 `passes`，loader 默认创建一个 Forward pass 使用全局 shader。

## Template 和 Instance 的边界

Template 决定**能力上限**，instance 决定**运行时实际状态**：

| 属于 template | 属于 instance |
|--------------|--------------|
| 支持哪些 pass | 启用了哪些 pass |
| 每个 pass 的 shader 和 variants | 参数值（UBO 字节） |
| render state | 纹理资源 |
| 反射出的 binding 结构 | per-pass 参数覆写 |

这条边界也是共享 instance、scene 重验证和 pipeline 身份分离的基础。

## 关键 API

| 方法 | 作用 |
|------|------|
| `setPass(pass, definition)` | 注册一个 pass |
| `getEntry(pass)` | 取某个 pass 的定义 |
| `buildBindingCache()` | 构建 per-pass material-owned binding 列表 |
| `getMaterialBindings(pass)` | 取某个 pass 的 material-owned bindings |
| `findMaterialBinding(id)` | 跨 pass 按名字查找 binding |
| `getPipelinePassSignature(pass)` | 导出某个 pass 的结构签名（用于 pipeline identity） |

## 继续阅读

- 蓝图里的 shader 到底是什么：[shader.md](shader.md)
- 蓝图为什么会直接影响 pipeline：[template-and-pipeline.md](template-and-pipeline.md)
