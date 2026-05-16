# 04 Gooch Shader

Gooch shader 是非真实感渲染的入门例子。它不像 PBR 那样追求物理正确，而是用冷色和暖色强调物体形体：背光处偏蓝，受光处偏黄。

## Gooch 的目标是把明暗变成冷暖

普通 Lambert 像用一盏灯照模型，暗面会越来越黑。Gooch 像美术老师用两支彩笔做明暗：暗面不是纯黑，而是冷色；亮面不是纯白，而是暖色。

## 片元公式

核心只有三步：

1. 算 `N dot L`。
2. 把 `[-1, 1]` 映射到 `[0, 1]`。
3. 在 `coolColor` 和 `warmColor` 之间插值。

```glsl
float ndl = dot(normalize(N), normalize(L));
float t = ndl * 0.5 + 0.5;
vec3 color = mix(coolColor.rgb, warmColor.rgb, t);
```

再乘一点 base color：

```glsl
color *= baseColor.rgb;
```

## Shader 参数

| 参数 | 含义 |
|---|---|
| `baseColor` | 物体本色 |
| `warmColor` | 受光侧色调 |
| `coolColor` | 背光侧色调 |
| `intensity` | 效果强度 |

这些参数应该出现在 shader uniform block 中，也应该出现在 `.material` 的 `parameters` 中。

## 当前光源输入

Gooch shader 可以先只读第一盏 directional light。更完整的多光源循环属于后续渲染路线；本教程的目标是验证材质接入，而不是实现完整光照模型。

## 我们已经学会了什么

Gooch shader 让我们看到：材质系统不仅能服务 PBR，也能服务实验渲染。只要 shader 和 `.material` 合同对齐，editor 就能调参数。

## 下一步

进入 [05 在 editor 中验证](05-verify-in-editor.md)。
