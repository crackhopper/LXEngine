# PBRT BMW M6 转换工具

`lxe_pbrt_scene_convert` 用来把 PBRT v3 的 `bmw-m6` 场景转换成 LXEngine 当前能读取的资产，同时保留未来高真实度渲染需要的 PBRT 原始材质信息。

## 输入数据

PBRT 数据目录应包含：

```text
data/pbrt-v3-scenes/pbrt-v3-scenes/bmw-m6/
  bmw-m6.pbrt
  geometry/*.ply
  textures/sky.exr
  spds/Al.eta.spd
  spds/Al.k.spd
  bsdfs/leather.bsdf
  BLENDSWAP_LICENSE.txt
```

## 转换命令

本地或远程都使用同一个命令。输出统一放在 `data/scenes/bmw-m6/`：

```bash
python3 src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py \
  --input data/pbrt-v3-scenes/pbrt-v3-scenes/bmw-m6/bmw-m6.pbrt \
  --out data/scenes/bmw-m6 \
  --scene data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml
```

CMake 目标等价于：

```bash
ninja RunPbrtBmwM6Convert
```

## 输出结构

```text
data/scenes/bmw-m6/
  pbrt_bmw_m6.scene.yaml
  pbrt_bmw_m6.converted.json
  pbrt_bmw_m6.conversion.md
  meshes/*.obj
  materials/runtime-pbr-approx/*.material
  materials/pbrt-source/*.pbrt-material.yaml
  textures/sky.exr
  spds/*.spd
  bsdfs/*.bsdf
  licenses/BLENDSWAP_LICENSE.txt
```

`pbrt_bmw_m6.scene.yaml` 是当前 renderer 读取的入口。它引用 OBJ mesh 和 runtime PBR 近似材质。

`materials/pbrt-source/*.pbrt-material.yaml` 是无损 source material：保留 PBRT 材质类型、参数类型、参数顺序、SPD/BSDF 引用、mix 关系和源码行号。当前 generic `.material` loader 不读取这些文件，但未来真实 PBRT 材质 renderer 应该以它们为权威输入。

## 当前离线渲染 smoke

当前 renderer 不支持 PBRT infinite HDR 环境光直接照亮表面，实时 editor 路径也还不能稳定加载 PBRT 的 `sky.exr`。转换工具会在 scene environment 中保留 `hdrUri`，但默认关闭 `environment.enabled` 和 `skyboxEnabled`，并额外生成一个 `pbrt_runtime_key_light` 作为 runtime approximation。原始 `LightSource "infinite"` 仍保留在 scene environment 和 manifest 中。

低成本 smoke render：

```bash
build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml \
  --profile offline-pbrt-reference \
  --width 320 \
  --height 228 \
  --samples 1 \
  --max-bounce 1 \
  --out artifacts/pbrt/bmw-m6/local-smoke/render
```

输出：

```text
artifacts/pbrt/bmw-m6/local-smoke/render.exr
artifacts/pbrt/bmw-m6/local-smoke/render.png
artifacts/pbrt/bmw-m6/local-smoke/render.json
```

## 未来全数据渲染路线

要让 BMW M6 使用 PBRT 原始数据做高真实度渲染，需要按以下方向扩展 renderer：

| 模块 | 需要使用的保留数据 |
|---|---|
| HDR environment importance sampling | `textures/sky.exr` 和 PBRT infinite light |
| Spectral metal | `spds/Al.eta.spd`、`spds/Al.k.spd` |
| PBRT glass / transmission | source material 中的 `pbrtType: glass` |
| Fourier BSDF | `bsdfs/leather.bsdf` 和 `pbrtType: fourier` |
| Mix material | `namedMaterialRefs` 和 `amount` |
| Substrate / car paint | `Kd`、`Ks`、`uroughness`、`vroughness` |

当这些模块具备后，`offline-pbrt-reference` profile 应让直接材质引用从 runtime PBR approximation 过渡到读取 `offline.pbrtSourceMaterialUri` 指向的 source material YAML。
