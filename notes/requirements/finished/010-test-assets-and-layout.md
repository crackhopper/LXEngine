# REQ-010: 测试资产基线与 `assets/` 目录约定

## 背景

为了支持 [Phase 1 渲染深度](../../notes/roadmaps/main-roadmap/phase-1-rendering-depth.md)（PBR / shadow / IBL / bloom / FXAA），仓库需要一套可被 demo、集成测试和后续 loader 共同消费的测试资产基线。

2026-04-16 按当前代码核查，仓库现状仍然是：

- 根目录存在 `models/viking_room.obj` 与 `textures/viking_room.png`
- 尚无顶层 `assets/` 目录
- 尚无 `cdToWhereAssetsExist()` helper
- 顶层 `CMakeLists.txt` 尚无资产同步规则
- [notes/concepts/assets/index.md](../../notes/concepts/assets/index.md) 已把“`assets/` 目录约定、测试资产基线”写成“已实现”，但代码与目录结构尚未兑现

如果不先把这层基础设施立住，后续需求会同时遇到几类问题：

- `REQ-011` 没有稳定的 glTF 测试输入位置
- `REQ-019` 没有统一的 demo 资产入口
- `REQ-028` 没有约定好的环境贴图位置
- 文档与实现状态继续漂移，后续判断“什么已经完成”会失真

本需求只解决测试资产基线、仓库目录约定、路径定位 helper、构建期同步与文档校准；不实现任何新的 loader，不修改渲染逻辑。

## 目标

1. 在仓库内引入一个可追溯、可直接提交的 `assets/` 顶层目录
2. 将测试资产总量控制在 **100 MB 以内**
3. 提供统一的 `cdToWhereAssetsExist(subpath)` helper，供 demo 与测试从不同工作目录定位资产
4. 让 build 目录与源码目录都能稳定访问 `assets/`
5. 修正文档中对“资产基线已实现”的错误现状描述

## 需求

### R1: `assets/` 目录基线

仓库新增顶层 `assets/`，最小结构如下：

```text
assets/
├── models/
│   ├── damaged_helmet/
│   │   ├── DamagedHelmet.gltf
│   │   ├── DamagedHelmet.bin
│   │   ├── *.png
│   │   └── README.md
│   ├── sponza/
│   │   ├── Sponza.gltf
│   │   ├── ...
│   │   └── README.md
│   ├── stanford_bunny/
│   │   ├── bunny.obj
│   │   └── README.md
│   └── viking_room/
│       ├── viking_room.obj
│       └── README.md
├── textures/
│   └── viking_room/
│       ├── viking_room.png
│       └── README.md
└── env/
    ├── studio_small_03_2k.hdr
    └── README.md
```

约束：

- 根目录旧 `models/` 与 `textures/` 的内容必须迁入 `assets/`
- 引用方后续统一通过 `assets/...` 相对路径访问
- `assets/` 下允许保留少量说明文件，但不引入额外的下载脚本、子模块或外部包管理
- 本 REQ 不要求做运行时“资产注册表”或 `AssetManager`

### R2: 资产清单与体积预算

本 REQ 要求把以下资产直接纳入仓库：

| 资产 | 用途 | 2026-04-16 实测/现有体积 |
|---|---|---:|
| `DamagedHelmet` | PBR 主测试模型 | `3,776,265 B` |
| `Sponza` glTF 版 | shadow / 多 mesh / culling 压力场景 | `52,686,624 B` |
| `studio_small_03_2k.hdr` | IBL 环境贴图输入 | `6,673,807 B` |
| `viking_room.obj` | 兼容旧 demo | `479,536 B` |
| `viking_room.png` | 兼容旧 demo | `962,052 B` |
| `Stanford Bunny` | 经典 baseline 模型 | 以实际引入文件为准 |

已实测的前五项合计约 `64.58 MB`，因此在 `Stanford Bunny` 选型合理的前提下，总量应保持在 `100 MB` 以内。

约束：

- 最终提交到仓库的 `assets/` 总大小必须 **<= 100 MB**
- 若 `Stanford Bunny` 的实际版本导致总量超限，裁剪顺序如下：
  1. 优先替换为更小的 bunny 表示形式（例如更轻量的 `.obj` 版本）
  2. 若仍超限，移除 bunny，并在 `README.md` 记录原因
  3. 不移除 `DamagedHelmet`、`Sponza`、`studio_small_03_2k.hdr`、`viking_room`
- 不引入 4K 及以上 HDR
- 不引入 Bistro、Cornell Box 或其他额外大场景

### R3: 来源、License 与资产说明

`assets/` 下每个顶级资产目录或单文件归属目录必须带 `README.md`，至少包含：

- 资产名
- 来源 URL
- 原始 license
- 本仓库中的用途
- 关键文件列表
- 文件大小
- 若可获得，则补充三角面数 / mesh 数 / 纹理张数

此外，`assets/README.md` 或等价总览文件必须汇总：

- 全部引入资产清单
- 总体积统计
- 超过 `100 MB` 时的裁剪原则
- 哪些后续需求会消费这些资产

### R4: `cdToWhereAssetsExist(subpath)` helper

在现有 `src/core/utils/filesystem_tools.hpp/.cpp` 中新增：

```cpp
bool cdToWhereAssetsExist(const std::string& subpath);
```

行为要求：

- 从当前工作目录开始向上搜索
- 当发现 `<dir>/assets/<subpath>` 存在时，将 cwd 切换到该目录并返回 `true`
- 搜索失败返回 `false`
- 搜索深度与现有 `cdToWhereShadersExist()` 保持同一数量级，避免无界向上遍历

范围约束：

- 本 REQ 只新增 `cdToWhereAssetsExist()`，不顺带重构 `filesystem_tools` 的命名空间或接口风格
- 由于当前 `cdToWhereShadersExist()` 仍是全局函数，本 helper 可以先沿用同一风格，命名空间收敛留给独立整理需求

### R5: CMake 构建期可访问性

顶层 `CMakeLists.txt` 必须增加 `assets/` 的构建期同步方案，使以下场景都能访问资产：

- 从源码根目录启动 demo
- 从 `${CMAKE_BINARY_DIR}` 启动 demo
- 从测试二进制工作目录启动集成测试

允许实现方式：

- 优先使用 symlink（平台允许时）
- Windows 或 symlink 不可用时，回退到 copy

约束：

- 不要求每次 build 全量拷贝；若采用 copy，需尽量复用 CMake 依赖关系避免无意义重复同步
- 即便 build 目录同步失败，`cdToWhereAssetsExist()` 仍必须能作为兜底路径定位手段

### R6: 测试

新增 `src/test/integration/test_assets_layout.cpp`，至少覆盖：

- `cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf") == true`
- `cdToWhereAssetsExist("env/studio_small_03_2k.hdr") == true`
- `cdToWhereAssetsExist("models/viking_room/viking_room.obj") == true`
- `cdToWhereAssetsExist("nonexistent/foo.bar") == false`

测试范围约束：

- 本 REQ 只验证目录、路径与文件存在性
- 不验证 glTF、OBJ、HDR 文件内容是否可被正确解析
- 不验证渲染结果

### R7: 文档现状修复

实现本 REQ 时，必须同步修正所有把“测试资产基线已完成”写成既成事实、但当前代码尚未兑现的文档。

至少包括：

- [notes/concepts/assets/index.md](../../notes/concepts/assets/index.md)

修正要求：

- 在本 REQ 完成前，不得再将 `assets/` 基线描述为“已实现”
- 在本 REQ 完成后，文档内容必须与实际仓库目录、helper、测试覆盖一致
- 若相关 roadmap、概念文档或需求索引提到旧根目录 `models/` / `textures/`，实现时一并更新为 `assets/` 约定

## 测试

- 新增 `test_assets_layout`
- 构建验证：
  - 从源码目录运行测试可找到资产
  - 从 build 目录运行测试可找到资产
- 抽样人工检查：
  - `assets/README.md` 体积统计与仓库实物一致
  - `README.md` 中来源与 license 信息可追溯

## 修改范围

| 文件/目录 | 改动 |
|---|---|
| `assets/**` | 新增目录、资产文件、README 与 license 说明 |
| `models/**` | 删除，内容迁入 `assets/models/` |
| `textures/**` | 删除或迁入 `assets/textures/` |
| `src/core/utils/filesystem_tools.hpp` | 新增 `cdToWhereAssetsExist` 声明 |
| `src/core/utils/filesystem_tools.cpp` | 新增实现 |
| `CMakeLists.txt` | 新增构建期资产同步规则 |
| `src/test/integration/test_assets_layout.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |
| 直接引用旧 `models/` / `textures/` 的调用点 | 更新到 `assets/` 路径 |
| `notes/concepts/assets/index.md` 及相关文档 | 修正错误现状描述 |

## 边界与约束

- 不实现 OBJ、glTF、HDR 的任何 loader
- 不引入 git submodule
- 不把大型测试资产下载到 build 目录后再临时使用；资产本身就是仓库内容
- 不引入 git LFS 作为本 REQ 的前提
- 不修改渲染逻辑
- 不做统一 `AssetManager`

## 依赖

- 无

## 下游

- `REQ-011`：以 `assets/models/damaged_helmet/` 作为 glTF 测试输入
- `REQ-019`：以 `DamagedHelmet`、`Sponza` 作为 demo 场景资产
- `REQ-028`：以 `assets/env/studio_small_03_2k.hdr` 作为环境贴图输入
- 后续任何资产管线需求：复用 `assets/` 约定与路径定位 helper

## 实施状态

2026-04-16 实施完成。

- `assets/` 顶层目录已建立，含 `models/`、`textures/`、`env/` 子目录
- DamagedHelmet (3.7 MB)、Sponza (51 MB)、Stanford Bunny (2.3 MB)、viking_room、studio_small_03_2k.hdr (6.4 MB) 均已就位，总计 ~65 MB
- `cdToWhereAssetsExist(subpath)` helper 已实现于 `filesystem_tools.hpp/.cpp`
- 顶层 CMake 已添加 symlink 优先 / copy 兜底的资产同步
- `test_assets_layout` 集成测试已通过（4 个用例）
- 根目录旧 `models/` 和 `textures/` 已删除
- 文档已修正为实际状态
