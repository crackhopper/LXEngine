# 05 在 editor 中验证：什么才算材质接入完成

写完 shader 不算完成。材质必须能挂到节点上，参数能改，场景能保存，再重新加载仍然正确。

## 只看到颜色变化还不够

验证材质像试菜：

| 步骤 | 类比 | editor 里做什么 |
|---|---|---|
| 上菜 | 把材质挂到物体 | 改 `materialUri` |
| 调味 | 改参数 | Inspector 节点级覆盖 |
| 留菜单 | 保存结果 | `scene save` |
| 复查 | 重开当前 project 的场景 | `scene open` |

如果只看见画面颜色变了，我们只能说明 shader 大概率执行了；还不能说明 `.material`、Inspector、node override 和 scene round-trip 都接通了。

## 挂到节点并确认 materialUri

1. 启动 `lxe_editor`。
2. 创建一个 cube 或 sphere。
3. 在 Inspector 的 Material 区域把材质切到 `assets/materials/gooch_demo.material`。

这一步要确认选中节点的 `materialUri` 指向新材质。材质挂不上时，先回到 `.material` 路径、shader 编译和 loader 错误。

## 修改参数并确认 Inspector override

4. 修改节点级参数，例如 `warmColor`、`coolColor`。这一步验证的是 editor 是否能把节点级覆盖写入 scene document，而不是只改了某个临时 UI 状态。

## 保存和重新加载确认 round-trip

5. 保存场景：

```text
scene save
```

6. 重新加载：

```text
scene open main
```

## 读 YAML 确认最终记录形态

```yaml
materialUri: assets/materials/gooch_demo.material
nodeMaterialOverrides:
  parameters:
    MaterialUBO.warmColor: [1.0, 0.8, 0.25, 1.0]
    MaterialUBO.coolColor: [0.1, 0.25, 0.8, 1.0]
```

字段名要以当前 shader / material 真实 binding 为准。这里的 `MaterialUBO` 只是示例。

## 我们已经学会了什么

材质验证不只是“画面变了”。我们还要验证 editor 可编辑、scene file 可保存、重新加载可还原。

## 下一步

进入 [06 材质排错](06-debug-material-problems.md)。
