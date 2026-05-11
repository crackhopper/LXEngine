# REQ-041-e: 测试场景编辑 v1 — Inspector 的材质 / 颜色 / 可见性收敛

> 2026-05-11 重整：旧的 `041-f` 以“菜单栏 / 主题 / chrome”展开，但按现有代码与当前目标审计，这不是测试场景搭建的关键路径。真正阻塞使用的是 Inspector 还不能直接改材质、改基础颜色，且 `visibilityMask` 的 32 位勾选 UI 过重。

## 背景

当前 `InspectorPanel` 已经能编辑：

- 节点 name / translation / rotation / scale
- `visibilityMask`
- camera 的 `fov / near / far / projection / cullingMask`
- directional light 的 `direction / color / intensity`

但它在“快速搭测试场景”上还缺两块关键信息：

1. 对 mesh/material 节点，只显示 `Material: yes/no`，不能改材质来源，也不能改常见的基础颜色
2. `visibilityMask` 与 `camera cullingMask` 目前用 32 个 bit checkbox 展开，信息密度过高，不适合日常编辑

## 目标

1. Inspector 能直接编辑节点使用的材质来源
2. 颜色等常用外观参数默认作为 `SceneNode` 级覆盖存在，而不是直接改动材质资产
3. Inspector 提供显式入口，把当前节点覆盖值写回材质
4. `visibilityMask` 改成单值表达，降低面板复杂度
5. 改完后能稳定保存回 scene file

## 需求

### R1: `visibilityMask` 从位图矩阵改成单值输入

- 节点 `visibilityMask` 不再显示 32 个 bit checkbox
- 改成单一字段，默认以 `0xFFFFFFFF` 形式显示，同时接受十进制输入
- 按 Enter 或失焦提交，底层仍走现有 `set <path>.visibilityMask <u32>`
- snapshot 摘要区只显示一个值，不再重复“文本 + 位图编辑器”两套表达

### R2: camera `cullingMask` 也使用同样的单值编辑方式

- 与节点 `visibilityMask` 保持一致：单值输入，不展开 bit matrix
- 底层仍走现有 `set <path>.cullingMask <u32>`
- 本 REQ 不做按 Layer 名称分组的高级 UI；先把“太复杂”降下来

### R3: 对有材质的节点新增 Material 区块

当节点具备 `MeshComponent + MaterialComponent` 或 scene document 上有 `materialUri` 时，Inspector 展示：

| 字段 | 含义 |
|---|---|
| Material URI | 当前材质来源 |
| Preset | 常用测试材质快捷切换 |
| Base Color Override | 当前 `SceneNode` 对基础颜色的覆盖 |
| Apply To Material | 把当前节点覆盖显式写回材质 |

Preset 首批只允许选择仓库里已经存在的材质：

- `assets/materials/blinnphong_lit.material`
- `assets/materials/blinnphong_default.material`
- `assets/materials/blinnphong_textured.material`
- `assets/materials/pbr_gold.material`

要求：

- 选择 preset 时，同时更新运行时材质实例与 scene document 中的 `materialUri`
- 若当前节点没有显式 `materialUri`，但属于 builtin primitive，也要在第一次修改时补齐可保存的 `materialUri`
- 不引入任意文件路径选择器；本 REQ 先做“常用测试材质”

### R4: 支持编辑节点级基础颜色覆盖

- 对存在 `MaterialUBO.baseColor` 参数的材质，Inspector 显示 `ColorEdit3`
- 默认行为不是改材质资产，而是给当前 `SceneNode` 增加一个 node-level override
- 改色后需要同时更新：
  - 运行中的节点实例外观
  - 保存用的 scene document 节点数据
- 为了能 round-trip，scene document 需要把颜色覆盖保存在节点上，而不是保存在材质上。推荐形状：

```yaml
material:
  uri: assets/materials/blinnphong_lit.material
nodeMaterialOverrides:
  baseColor: [0.8, 0.2, 0.2]
```

- 若材质不暴露 `MaterialUBO.baseColor`，则该控件隐藏或 disabled，并说明原因
- v1 只支持 `baseColor` 一个常用覆盖，不把材质系统完整反射面一次搬进 Inspector

### R5: Inspector 提供显式“覆盖到材质”入口

- Material 区块提供一个按钮，例如 `Apply Override To Material`
- 该操作是显式的、一次性的，不是默认行为
- 按下后：
  - 以当前节点的 `baseColor` 覆盖值更新运行中的材质实例
  - 同步更新 scene document 中该节点引用的材质覆盖数据
- 若当前材质 URI 指向共享材质资产，UI 必须明确提示这是“覆盖材质”的动作，不再只是当前节点生效
- v1 明确定义为“更新当前 scene document 中该节点引用的材质配置/覆盖”
- 该操作 **不**回写磁盘上的原始 `.material` 资产文件；它只改变当前 scene file 内记录的材质侧覆盖语义
- 因此，`Apply Override To Material` 的效果边界是“当前 scene document 内所有引用这份材质配置的节点”，而不是整个仓库的材质资产
- 语义上必须与“仅当前节点生效”的 node-level override 清楚区分

### R6: 命令总线提供统一修改入口

Inspector 的材质编辑不得直接改内存后绕过 history。需要补齐统一命令入口，至少覆盖：

- `set <path>.materialUri <uri>`
- `set <path>.nodeMaterial.baseColor <r> <g> <b>`
- `apply_material_override <path> baseColor`

要求：

- 成功结果带 structured 输出，便于后续测试与自动化消费
- 这些修改支持现有 undo / redo
- 保存场景时以 scene document 当前值为准，而不是反向猜测 runtime material 状态
- `set <path>.nodeMaterial.baseColor` 的默认语义必须是“只改当前节点”
- `apply_material_override` 必须是显式命令，不允许 Inspector 在普通改色时自动触发
- `apply_material_override` 的持久化目标是当前 scene document，不是源 `.material` 资产文件

### R7: 测试覆盖

至少补以下测试：

- `InspectorPanel::makeSnapshot()` 或等价路径能识别节点当前材质 URI / node-level baseColor override 是否可编辑
- `set <path>.materialUri ...` 会切换运行时材质，并能保存后再加载
- `set <path>.nodeMaterial.baseColor r g b` 只影响当前节点实例外观，不影响其他引用同材质的节点
- scene document round-trip 后，节点级 `baseColor` 覆盖仍保留
- `apply_material_override <path> baseColor` 仅在显式触发时才更新材质侧数据，且结果只落在当前 scene document
- visibility / culling mask 单值输入仍能正确更新底层 mask
- CPU-only ImGui 帧下，含材质区块的 Inspector 绘制不崩溃

## 修改范围

- `src/core/editor/inspector_panel.{hpp,cpp}`
- `src/core/editor/commands/builtin_commands.cpp`
- `src/demos/scene_viewer/scene_document.{hpp,cpp}`
- `src/demos/scene_viewer/scene_runtime.cpp`
- `src/test/integration/test_inspector_panel.cpp`
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_scene_runtime.cpp`

## 边界与约束

- 本 REQ 不做通用材质反射编辑器；先只覆盖测试场景高频的 `materialUri + node-level baseColor`
- 本 REQ 不做 texture slot 选择器 / 文件对话框
- 本 REQ 不引入 layer 名称系统；mask 仍是 `u32`，只是 UI 表达更简单
- directional light 的颜色编辑已经存在，本 REQ 不重写其语义，只补 material 这条缺口
- 默认改色只对当前节点生效；“写回材质”必须是用户显式触发的副作用更大的动作
- 本 REQ 不支持把覆盖直接写回仓库中的 `.material` 资产文件

## 依赖

- [REQ-041-d](041-d-scene-authoring-toolbar-palette.md) — primitive / camera / light 创建后，需要能继续在 Inspector 里细调
- [REQ-041-a](finished/041-a-imgui-editor-mvp.md)
- [REQ-041-b](finished/041-b-command-bus-v2.md)

## 后续工作

- 若后续需要更多材质参数，再单独立 REQ 做“材质 Inspector v2”
- 若场景里真的出现 layer 语义，再考虑把 mask 单值升级成“值 + 命名 layer 辅助”

## 实施状态

待实施。优先级紧跟 `041-d`，因为创建出来的测试对象如果不能直接换材质和改颜色，仍然要回到手写 YAML，目标没有真正达成。
