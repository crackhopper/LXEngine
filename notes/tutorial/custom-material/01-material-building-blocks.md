# 01 材质积木：shader 为什么还需要菜谱

我们先不写代码，先解决一个最容易混淆的问题：shader 不是材质。shader 只说明“怎么计算”，材质还要说明“这套计算在什么 pass 里用、默认参数是什么、资源怎样绑定、运行时参数怎样保存”。

## 新人最容易混淆的边界

| 对象 | 类比 | 真实角色 |
|---|---|---|
| Shader | 烹饪方法 | 决定每个顶点和像素怎么算 |
| `.material` | 菜谱 | 声明 shader、pass、参数默认值和资源 |
| `MaterialTemplate` | 标准菜谱结构 | 按 pass 保存 shader 与 render state |
| `MaterialInstance` | 加载后的可调菜谱 | 由 `.material` 创建出来，持有 canonical 参数 buffer、texture 和 pass 开关 |
| node override | 单桌口味调整 | 场景节点针对这份材质参数的局部覆盖 |
| Mesh / vertex data | 原材料 | 提供顶点、法线、UV 等几何输入 |
| 屏幕像素 | 真正端上桌的菜 | shader、材质参数、mesh、灯光和相机共同渲染出的结果 |

这里要特别小心：`MaterialInstance` 名字里带有 `Instance`，但它不是“已经画出来的一盘菜”。在当前实现里，`.material` 经过 `loadGenericMaterial()` 加载后，会先构造 `MaterialTemplate`，再创建 `MaterialInstance`，最后把 YAML 里的 `parameters` 和 `resources` 写进这个 instance。所以它更像一份已经填好默认调味、可以继续微调的菜谱副本。

真正的一盘菜不是 C++ 里的某个材质对象，而是一次 draw 最后落到屏幕上的像素。那道菜需要原材料，也就是 mesh / vertex buffer；需要烹饪方法，也就是 shader；需要菜谱参数，也就是 `MaterialInstance` 里的 buffer 和 texture；还需要场景里的相机、灯光、transform。少了任何一环，都不能得到最终画面。

node override 处在更靠近场景的一层。它不会重新定义 shader、pass 或 pipeline，也不会把 mesh 变成材质；它只是在某个节点引用同一份 `.material` 时，对参数做局部覆盖。例如同一个 `gooch_demo.material` 可以挂在多个 sphere 上，每个 sphere 通过 `nodeMaterialOverrides` 调整 `MaterialUBO.warmColor`，从而得到不同的像素颜色。

## Template 与 Instance 分别负责什么

| 属于 template | 属于 instance |
|---|---|
| 哪个 pass 用哪个 shader | 当前参数值 |
| render state | 当前 texture 资源 |
| shader 反射出的 binding 结构 | dirty / GPU upload 状态 |
| pipeline identity 相关结构 | pass 启用状态 |

这个边界很重要：改 template 像改菜谱，会影响 pipeline；改 instance 参数像调味，通常只更新 uniform buffer。

node override 不在 template / instance 的结构边界里。它保存在 scene document 上，加载节点材质时按顺序叠加到新创建的 `MaterialInstance`：先应用 `.material` 默认值，再应用材质覆盖，最后应用节点覆盖。覆盖完成后，renderer 看到的仍然是一个普通 `MaterialInstance`。

## 读一个材质时的顺序

读材质时先按这个顺序：

1. 打开 `.material`，确认 shader 名和 pass。
2. 打开对应 `.vert` / `.frag`，确认 uniform block 和参数名。
3. 对照 `generic_material_loader.cpp`，理解 loader 如何把 YAML 写进 `MaterialInstance`。
4. 在 editor 中切换 `materialUri`，看 Inspector 是否能编辑节点级参数。

| 路径 | 读它的原因 |
|---|---|
| `notes/concepts/material/index.md` | 从概念层理解 template / instance |
| `notes/subsystems/material-system.md` | 当前实现形状 |
| `src/core/asset/material_template.hpp` | template 的 C++ 表达 |
| `src/core/asset/material_instance.hpp` | instance 的 C++ 表达 |
| `src/infra/material_loader/generic_material_loader.cpp` | YAML 到 runtime 的桥 |

## 我们已经学会了什么

材质系统不是“shader 文件等于材质”。材质是一份菜谱加一份运行时参数状态。

## 下一步

进入 [02 YAML 与 Shader 合同](02-material-yaml-and-shader-contract.md)。
