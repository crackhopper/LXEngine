# 01 · PBR 理论最小集

> 目的：在一页之内建立 Cook–Torrance PBR 着色所需的全部心智模型。不追求数学严谨，追求"每一行代码我知道对应哪一项"。

## 1. 渲染方程的工程化简

物理渲染的起点是渲染方程：

$$
L_o(p, \omega_o) = \int_{\Omega} f(p, \omega_i, \omega_o)\,L_i(p, \omega_i)\,(N \cdot \omega_i)\,d\omega_i
$$

$L_o$ 是一点 $p$ 往出射方向 $\omega_o$ 的辐亮度，$f$ 是 BRDF，$L_i$ 是从入射方向 $\omega_i$ 来的入射辐亮度，$N \cdot \omega_i$ 是几何项（Lambert cosine）。

**教程中的简化**：
- 只考虑一个方向光，积分退化成单次求值
- 没有环境光探针，环境项用 $ambient = 0.03 \cdot albedo \cdot ao$ 糊一下
- 不做 shadow map，$L_i$ 直接等于光的颜色

于是实际代码里每个像素算的是：

$$
L_o = BRDF(L, V, N, material) \cdot lightColor \cdot \max(N \cdot L, 0)
$$

$L$ 是到光源的方向，$V$ 是到相机的方向，$N$ 是法线。

---

## 2. Cook–Torrance BRDF

把 BRDF 拆成两项：

$$
f = k_D \cdot f_{lambert} + k_S \cdot f_{cook\text{-}torrance}
$$

- **Lambert 漫反射项**：$f_{lambert} = \dfrac{albedo}{\pi}$
- **Cook–Torrance 镜面项**：$f_{cook\text{-}torrance} = \dfrac{D \cdot G \cdot F}{4 \cdot (N \cdot V)(N \cdot L)}$

$k_S$ 是 Fresnel（反射比例），$k_D = 1 - k_S$（能量守恒，剩下的是漫反射）。对金属还要额外乘 $1 - metallic$，因为纯金属没有漫反射。

### 2.1 D — 法线分布函数 (GGX / Trowbridge–Reitz)

描述"微表面法线朝向半向量 `H` 的比例"。粗糙度越大，分布越散。

$$
a = roughness^2
$$

$$
D(H) = \frac{a^2}{\pi \cdot \left((N \cdot H)^2 \cdot (a^2 - 1) + 1\right)^2}
$$

### 2.2 G — 几何遮蔽项 (Smith + Schlick-GGX)

描述"微表面互相遮蔽 + 自遮蔽"的概率。用 Smith 方法把入射和出射两端分别算一次再相乘。

$$
k = \frac{(roughness + 1)^2}{8}
$$

$$
G_1(v) = \frac{N \cdot v}{(N \cdot v)(1 - k) + k}
$$

$$
G = G_1(V) \cdot G_1(L)
$$

### 2.3 F — 菲涅尔 (Schlick 近似)

描述"不同入射角下反射比例"。掠射角时接近 1，正入射时等于 $F_0$。

$$
F(H, V) = F_0 + (1 - F_0) \cdot (1 - H \cdot V)^5
$$

$$
F_0 = mix(0.04, albedo, metallic)
$$

$0.04$ 是大部分电介质（塑料、陶瓷、皮革、木头等）近似的正入射反射比。

---

## 3. Metallic-Roughness Workflow

PBR 的 artist 输入抽象成两个标量 + 一个 RGB：

| 参数 | 含义 | 典型值 |
|------|------|-------|
| `baseColor` (vec3) | 漫反射率（非金属）/ 反射率（金属） | 物体本色 |
| `metallic` (float) | 金属度，0 = 电介质，1 = 纯金属 | 极值居多，中间值仅用于脏污 / 过渡 |
| `roughness` (float) | 微表面粗糙度，0 = 镜面，1 = 完全漫反射 | 任意 |
| `ao` (float) | 环境光遮蔽，压低凹陷处环境项 | 0–1 |

这就是本教程 `MaterialUBO` 里全部的内容。没有纹理，**全由 UBO 标量驱动**。

---

## 4. 能量守恒的代码实现

```glsl
vec3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);
vec3 kS = F;
vec3 kD = vec3(1.0) - kS;
kD *= 1.0 - metallic;                // 金属没漫反射

vec3 numerator   = D * G * F;
float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 1e-4;
vec3 specular    = numerator / denominator;

float NdotL = max(dot(N, L), 0.0);
vec3 Lo = (kD * albedo / PI + specular) * lightColor * NdotL;
```

`+ 1e-4` 是为了避免 $N \cdot V = 0$ 时除零（相机掠射到物体边缘）。

---

## 5. 色调映射 + Gamma

PBR 的输出在线性空间可能 > 1.0（强高光）。直接写进 sRGB framebuffer 会被硬 clip。两步收尾：

```glsl
// 1. Tone map (Reinhard，最简单)
color = color / (color + vec3(1.0));

// 2. Gamma (sRGB 编码)
color = pow(color, vec3(1.0 / 2.2));
```

如果 swapchain 本身就是 `VK_FORMAT_*_SRGB`（本项目是），gamma 这一步在 Vulkan 里其实是由硬件完成的，我们**不该**再手动做。但教程为了可移植 + 调试方便，保留显式 gamma —— 如果在 sRGB framebuffer 上看起来偏亮，把这一行删掉即可。

---

## 6. 心智模型速查

```text
对每个片元：
  ├─ N, V, L, H  (几何方向)
  ├─ F0 = mix(0.04, albedo, metallic)
  ├─ D(GGX)       -> 微表面朝向 H 的数量
  ├─ G(Smith)     -> 可见比例
  ├─ F(Schlick)   -> 反射比例
  ├─ specular = D*G*F / (4*NV*NL)
  ├─ kD = (1-F) * (1-metallic)
  ├─ diffuse = kD * albedo / pi
  ├─ Lo = (diffuse + specular) * lightColor * max(N*L, 0)
  ├─ ambient = 0.03 * albedo * ao
  ├─ color = ambient + Lo
  ├─ tone map
  └─ gamma
```

下一章：把这套理论翻译成 GLSL，并对照当前仓库里的 `shaders/glsl/pbr.vert` / `pbr.frag`。

→ [02-pbr-shader.md](02-pbr-shader.md)
