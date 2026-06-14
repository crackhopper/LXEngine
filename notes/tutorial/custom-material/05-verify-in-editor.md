# 05 在 editor 中验证：什么才算材质接入完成

写完 `.material` 不算完成。材质必须能挂到节点上，active RenderPathGraph 能匹配它，参数覆盖能保存，再重新加载仍然正确。

## 只看到颜色变化还不够

| 步骤 | editor 里做什么 | 验证点 |
|---|---|---|
| 挂材质 | 改 `materialUri` | 节点引用的是新 `.material v2` |
| 匹配 pass | 使用当前 render profile / graph | `filters.renderClass` / `filters.bsdf` 命中 |
| 改参数 | Inspector 节点级覆盖 | 覆盖的是 BSDF 参数 envelope |
| 留记录 | `scene save` | scene YAML 保存 material URI 和 overrides |
| 复查 | `scene open` | 重新加载后画面和参数一致 |

如果只看见画面颜色变了，我们只能说明某条 shader 大概率执行了；还不能说明 material parser、graph filter、scene override 和 round-trip 都接通。

## 挂到节点并确认 materialUri

1. 启动 `lxe_editor`。
2. 创建一个 cube 或 sphere。
3. 在 Inspector 的 Material 区域把材质切到 `assets/materials/gooch_demo.material`、`assets/scenes/generated/materials/damaged_helmet_standard_pbr.material` 或其他新的 `.material v2` 文件。

这一步要确认选中节点的 `materialUri` 指向新材质。材质挂不上时，先看 schema、resource URI 和 parser 诊断。

## 修改参数并确认 Inspector override

修改节点级参数，例如：

```yaml
materialOverrides:
  roughness: { kind: float, value: 0.2 }
  baseColor: { kind: rgb, value: [0.9, 0.75, 0.45] }
```

字段名要以 material contract 真实参数为准；override 与 `.material` 一样保存 typed envelope。

## 保存和重新加载确认 round-trip

```text
scene save
scene open main
```

重新加载后检查三件事：

| 检查 | 说明 |
|---|---|
| material URI 保持不变 | scene document round-trip 正常 |
| override 参数仍在 | editor 没有只改 runtime 临时状态 |
| graph pass 仍能产出 draw | material render class / BSDF type 与 graph filter 匹配 |

## 我们已经学会了什么

材质验证不只是“画面变了”。我们还要验证 `.material v2`、RenderPathGraph filter、editor override、scene 保存和重新加载这一整条链路。

## 下一步

进入 [06 材质排错](06-debug-material-problems.md)。
