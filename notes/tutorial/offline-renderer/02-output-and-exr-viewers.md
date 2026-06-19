# EXR 与 PNG 输出：保存实验数据，也保存可看的预览

Offline renderer 的输出像一份实验记录：EXR 保存原始测量值，PNG 保存肉眼快速检查用的冲印照片。我们不把两者混成一个文件，因为 path tracing 后续会需要 scene-linear HDR 数据、AOV、误差分析和可复现实验；普通图片查看器则需要已经 tone-mapped 的 LDR 预览。

## 一次运行会写出哪些文件

```bash
./build/src/tools/lxe_offline_render/lxe_offline_render \
  --scene assets/scenes/realtime_offline_compare_helmet_pbr.scene.yaml \
  --profile preview \
  --samples 1 \
  --width 64 \
  --height 64 \
  --out artifacts/offline/smoke
```

输出文件：

| 文件 | 内容 | 用途 |
|---|---|---|
| `smoke.exr` | scene-linear RGBA half float | 主结果、后续 AOV/数值分析 |
| `smoke.png` | ACES + gamma 2.2 后的 8-bit RGBA | 人工快速看图、文档截图 |
| `smoke.json` | scene/profile/buildInfo/output metadata | 复现实验参数 |
| `smoke.rgba32f` | raw RGBA32F readback | 调试 buffer、简单二进制检查 |

`--out` 可以指向 basename，也可以指向已存在目录：

| `--out` | 写出 |
|---|---|
| `artifacts/offline/smoke` | `smoke.exr` / `smoke.png` / `smoke.json` / `smoke.rgba32f` |
| `artifacts/offline/smoke.exr` | 同样按 `smoke` 作为 basename |
| 已存在目录 `artifacts/offline/run/` | `run/render.exr` / `run/render.png` / `run/render.json` / `run/render.rgba32f` |

## EXR 与 PNG 的颜色边界

| 输出 | 色彩空间 | 是否 tone mapping | 当前策略 |
|---|---|---|---|
| EXR | scene-linear HDR | 否 | RGBA half float，保留 HDR 数值 |
| PNG | display preview | 是 | exposure 1.0、ACES、gamma 2.2 |
| RGBA32F | scene-linear HDR | 否 | 四通道 float 原样写出 |

PNG 的 tone mapping 逻辑与实时 post shader 对齐：先乘 exposure，再走 ACES 或 Reinhard；当前离线输出层默认使用 ACES。EXR 不做 tone mapping，所以在普通图片工具里直接看 EXR 可能显得过亮、过暗或曝光不对；这不是渲染失败，而是查看器如何把 HDR 映射到显示器的问题。

## 配置 EXR 查看工具

我们推荐把 PNG 当作第一眼 smoke，把 EXR 交给明确支持 OpenEXR 的工具查看。

| 工具 | 平台 | 适合场景 | 配置方式 |
|---|---|---|---|
| XnView MP | Windows / macOS / Linux | 直接浏览单张 EXR | 安装 XnView MP 后用它打开 `.exr`；官网列出 OpenEXR 支持 |
| DJV | Windows / macOS / Linux | 看序列、VFX 风格检查 | 安装 DJV 后打开 `.exr` 或目录；它支持 OpenEXR image sequences |
| OpenImageIO `oiiotool` | Windows / macOS / Linux | 命令行检查 metadata / 像素 | 安装 OpenImageIO 后使用 `oiiotool --info -v smoke.exr` |
| VS Code / Cursor HDR 预览扩展 | 编辑器内 | 快速在代码编辑器里看 EXR | 安装 HDR/EXR 预览类扩展；部分扩展依赖 OpenImageIO |

参考入口：

- [XnView MP](https://www.xnview.com/en/)：官网说明支持 OpenEXR。
- [DJV](https://grizzlypeak3d.github.io/DJV/)：面向高 bit-depth image sequence 的查看器，支持 OpenEXR。
- [OpenImageIO `oiiotool`](https://openimageio.readthedocs.io/en/v3.0.16.0/oiiotool.html)：用于命令行 inspection / conversion。
- [VS Code HDR Preview](https://marketplace.visualstudio.com/items?itemName=mateh.exr-preview)：编辑器内 EXR 预览扩展，说明依赖 OpenImageIO 命令行工具。

## 我们如何确认输出是有效的

```bash
ls -lh artifacts/offline/smoke.*
python3 - <<'PY'
import json
from pathlib import Path
meta = json.loads(Path("artifacts/offline/smoke.json").read_text())
print(meta["exrStorage"], meta["pngPreview"], meta["width"], meta["height"])
PY
```

我们还可以用 `oiiotool` 做命令行检查：

```bash
oiiotool --info -v artifacts/offline/smoke.exr
```

如果没有安装 EXR 工具，先打开同 basename 的 PNG。PNG 和 EXR 来自同一份 `OfflineReadbackImage`，所以 PNG 有画面通常能说明 compute、readback 和基础颜色数据已经连通。

## 我们已经学会了什么

我们已经把离线输出分成了两类：EXR 是保留线性 HDR 数据的主结果，PNG 是快速看图的 tone-mapped preview。`smoke.json` 让实验参数可追踪，`.rgba32f` 让底层 buffer 仍可直接调试。

## 下一步

继续读 [实现结构](03-implementation-flow.md)，我们会把输出文件放回整条代码路径里，看清楚它和 scene loader、offline FrameGraph、Vulkan compute、readback 的关系。
