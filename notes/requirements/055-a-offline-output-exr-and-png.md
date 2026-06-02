# REQ-055-a: Offline Output EXR 与 PNG

> 2026-06-01 更新：本 REQ 的 MVP 已实现。`lxe_offline_render` 现在从同一份 linear float readback 写出 scene-linear EXR、tone-mapped PNG、JSON metadata 和 `.rgba32f` 调试 dump。

## 背景

离线渲染结果有两个用途：

1. 作为 ground truth 进行数值对比、归档和后续 AOV 分析。
2. 作为人眼可快速查看的 preview。

EXR 适合保存 scene-linear HDR float 数据和未来 AOV；PNG 适合快速查看、文档展示和简单回归对比。因此第一版应输出成对文件。

`REQ-054-b` 只负责把 Vulkan compute 的 HDR output/accumulation buffer read back
为 CPU 端 linear float buffer。本 REQ 接管该 buffer 的正式文件写出，并交付第一张
可查看的离线渲染图。

## 目标

1. 输出 scene-linear EXR。
2. 输出 tone-mapped PNG preview。
3. 明确 color space、tone mapping、文件命名和 metadata。
4. 为后续 AOV 输出预留结构。

## 需求

### R1: EXR 主输出

离线 renderer 主输出为 `.exr`。

要求：

- 保存 scene-linear HDR 数据。
- 首版至少保存 beauty RGBA。
- 内部 accumulation buffer 可使用 `RGBA32F`。
- 写出可选择 half float 或 float；首版默认 half float 可接受，但必须记录策略。
- 输出 metadata 至少包含 scene path、camera path、profile、sample count、max depth、seed 和 build id。

### R2: PNG preview 输出

同时输出 `.png` preview。

要求：

- 从同一份 linear buffer 生成。
- 使用明确 tone mapping。
- 使用 gamma/sRGB 转换。
- 默认文件名与 EXR 同 basename。

示例：

```text
artifacts/offline/ibl_metal_sphere.exr
artifacts/offline/ibl_metal_sphere.png
```

### R2.1: 输出路径约定

默认输出根目录：

```text
artifacts/offline/
```

建议默认结构：

```text
artifacts/offline/<scene-name>/<profile>/<timestamp-or-fixed-name>/
```

CLI `--out` 语义：

- 如果 `--out` 指向目录：写出 `render.exr`、`render.png`，可选 `render.json`。
- 如果 `--out` 指向 basename：写出 `<basename>.exr`、`<basename>.png`，可选 `<basename>.json`。
- 测试和 CI 应使用固定 basename，避免 timestamp 影响可复现性。

要求：

- `artifacts/` 不进入 git。
- 输出目录不存在时按约定创建，或在权限不足时给出明确错误。
- sidecar metadata JSON 可选，但推荐第一版就写出，内容与 EXR metadata 对齐。

### R3: Tone mapping 配置

首版可以复用实时 post stack 的 tone mapping 规则。

要求：

- 至少支持 exposure。
- 默认 tone mapping 与 editor realtime preview 尽量一致。
- 写入 PNG 前 clamp 到 `[0, 1]`。
- EXR 不做 tone mapping。
- PNG preview 在 CPU readback 后执行 tone mapping / gamma，不为了 preview 再跑一遍 realtime post-process pass。
- tone mapping 参数语义与 `REQ-046-a` 的标准 post-process 保持一致；实现可以是离线输出层的 CPU 版本。

### R4: EXR 依赖策略

首版 EXR writer 使用 TinyEXR。

理由：

- 集成成本低。
- 足够写出 beauty EXR / 简单 float image。
- 不需要第一版引入 OpenEXR 的较重依赖。
- 后续如果需要 multipart、多 channel、高级压缩或生产级 color metadata，可替换为 OpenEXR。

要求：

- TinyEXR 只能出现在 image writer 实现层。
- renderer core、GpuScene、offline job 不直接 include TinyEXR。
- 对外使用 `OfflineImageWriter` 或等价抽象。
- CMake 集成必须跨 Linux/Windows。

### R5: 错误诊断

输出失败必须包含：

- 目标路径
- 格式
- 失败原因
- 是否已经写出部分文件

### R6: 测试覆盖

覆盖：

- 写出 2x2 float buffer 到 EXR。
- 写出 2x2 float buffer 到 PNG。
- PNG preview 的 tone mapping/gamma 结果可预测。
- 输出目录不存在时按约定创建或报错。
- unsupported extension 给出诊断。

### R7: Demo 1 正式图片输出

基于 `REQ-054-b` 的 MVP 基准场景和 readback buffer，本 REQ 负责写出第一组成对图片：

- scene-linear EXR。
- tone-mapped PNG preview。
- metadata JSON 或 EXR metadata。

验收重点：

- EXR 与 PNG 来自同一份 readback linear float buffer。
- PNG 可直接用于人工查看 MVP 渲染结果。
- 输出路径、basename、metadata 与 CLI 参数一致。
- 重复运行同一 scene/profile/seed 时，输出内容可复现。

## 修改范围

- offline output writer 新模块
- `src/infra/` 或 `src/tools/` 图像写出封装
- CMake dependency 集成
- `REQ-054-a` 的 readback 接口
- tests

## 边界与约束

- 本 REQ 不实现 renderer。
- 本 REQ 不定义 AOV 列表；AOV 在 `REQ-057-a` 扩展。
- 本 REQ 不把 `.hdr` 作为离线输出主格式。
- `.hdr` 仍可作为环境贴图输入。

## 依赖

- `REQ-054-a`
- 当前 HDR/Post tone mapping 规则

## 后续工作

- `REQ-057-a` 添加 AOV EXR multi-channel 或 multi-file 输出。
- `REQ-058-a` 在 editor 中打开 PNG preview / 输出目录。

## 实施状态

MVP 已实现：

- `src/infra/offline/offline_image_writer.*` 提供 `OfflineImageWriter`。
- EXR 使用 TinyEXR，写出 RGBA half float scene-linear beauty。
- PNG 使用 stb_image_write，CPU 侧执行 exposure 1.0、ACES、gamma 2.2 preview。
- CLI `--out` 支持 basename、带扩展名路径和已存在目录。
- sidecar JSON 记录 scene path、scene name、camera path、profile、width/height、samples、max depth、seed、output format、EXR storage、PNG tone mapping 和合成后的 `buildInfo` 字符串。
- `.rgba32f` 继续写出，作为底层 readback 调试文件。
- `test_offline_image_writer` 覆盖 2x2 EXR/PNG/JSON/raw 输出、PNG signature、EXR magic、目录输出 basename 和 tone mapping。

未完成范围：

- AOV / variance / multipart EXR。
- UI 中打开 PNG preview 或输出目录。
- 可配置 tone mapping CLI 参数。
