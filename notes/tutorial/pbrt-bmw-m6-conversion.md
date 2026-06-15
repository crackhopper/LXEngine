# PBRT BMW M6 资产包

`lxe_pbrt_scene_convert` 用来把 PBRT v3 的 `bmw-m6` 场景转换成 LXEngine 当前能读取的资产，同时保留未来高真实度渲染需要的 PBRT 原始材质信息。当前版本库内采用的 BMW M6 数据包在 `assets/models/bmw-m6/`；文档入口只指向这个版本库资产包。

## 当前版本库资产

`assets/models/bmw-m6/` 已包含转换后的主包：

```text
assets/models/bmw-m6/
  pbrt_bmw_m6.scene.yaml
  pbrt_bmw_m6.converted.json
  pbrt_bmw_m6.conversion.md
  meshes/*.obj
  materials/runtime-pbr-approx/*.material
  materials/pbrt-source/*.pbrt-material.yaml
  textures/sky.exr
  spds/*.spd
  bsdfs/leather.bsdf
  licenses/BLENDSWAP_LICENSE.txt
```

当前包里有 114 个 OBJ mesh、28 份 runtime PBR approximation material、28 份 PBRT source material。`pbrt_bmw_m6.scene.yaml` 是 renderer 读取入口；`pbrt_bmw_m6.converted.json` 是转换 manifest；`pbrt_bmw_m6.conversion.md` 记录转换损失和未来高真实度路线。

`materials/runtime-pbr-approx/*.material` 是当前实时/离线 renderer 可消费的 `.material v2` 近似材质。`materials/pbrt-source/*.pbrt-material.yaml` 是 source-preserving 输入，保留 PBRT 材质类型、参数类型、SPD/BSDF 引用、mix 关系和源码行号；当前 generic `.material` loader 不直接读取这些文件。

## 重新生成资产包

如果要从 PBRT 原始场景重新生成版本库资产，输出应写回 `assets/models/bmw-m6/`：

```bash
python3 src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py \
  --input <pbrt-v3-scenes>/bmw-m6/bmw-m6.pbrt \
  --out assets/models/bmw-m6 \
  --scene assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml
```

`<pbrt-v3-scenes>` 是本机 PBRT v3 scene checkout 的位置，不是 LXEngine 仓库内的约定目录。重新生成后，检查 scene、manifest 和 report 中的 mesh/material/texture URI 都指向 `assets/models/bmw-m6/` 包内路径；不要依赖未纳入版本库的临时输出目录。

## 当前离线渲染 smoke

当前 renderer 不支持 PBRT infinite HDR 环境光直接照亮表面，实时 editor 路径也还不能稳定加载 PBRT 的 `sky.exr`。转换工具会在 scene environment 中保留 `hdrUri`，并额外生成一个 `pbrt_runtime_key_light` 作为 runtime approximation。原始 `LightSource "infinite"` 仍保留在 scene environment 和 manifest 中，供未来 HDR environment importance sampling 使用。

低成本 smoke render：

```bash
build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml \
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

如果 smoke 报告找不到 mesh、material 或 HDR 文件，先检查 `pbrt_bmw_m6.scene.yaml` 内的 URI 是否仍指向旧的临时输出路径。当前文档和版本库事实以 `assets/models/bmw-m6/` 为准。

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
