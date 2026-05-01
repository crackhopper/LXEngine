# REQ-011: `GLTFLoader` 真实加载与 PBR 元数据暴露

## 背景

当前仓库里的 `GLTFLoader` 仍然是桩实现，还不能为 Phase 1 的 PBR 调试链路提供真实输入。

2026-04-16 按当前代码核查：

- [src/infra/mesh_loader/gltf_mesh_loader.cpp](../../src/infra/mesh_loader/gltf_mesh_loader.cpp) 的 `GLTFLoader::load()` 只读取文件头，然后对 `.gltf` / `.glb` 一律抛 `not yet supported`
- [src/infra/mesh_loader/gltf_mesh_loader.hpp](../../src/infra/mesh_loader/gltf_mesh_loader.hpp) 目前只暴露 `positions` / `normals` / `texCoords` / `indices`
- 当前仓库没有任何 vendored 的 glTF 解析库
- 但 `infra` 层的第三方依赖接入方式已经不是“在线下载”，而是把需要的头文件、源文件或预编译库直接纳入仓库，然后离线构建：
  - `stb`、`tinyobjloader` 以 vendored header 方式存在于 `src/infra/external/include/`
  - `SPIRV-Cross`、`yaml-cpp` 以 vendored source tree 方式存在于 `src/infra/external/`
  - `SDL3` 以 vendored package 方式存在于 `src/infra/external/SDL3/`，按平台消费其头文件和库文件
- `REQ-010` 已经把 `DamagedHelmet` 定义为后续 glTF/PBR 测试输入，但 `GLTFLoader` 还无法消费它
- [notes/concepts/assets/index.md](../../notes/concepts/assets/index.md) 对“GLTF 已承载 PBR 元数据”的描述先于代码落地，需要等本 REQ 实现后才能成立

本需求的目标不是做一个“完整 glTF 资产管线”，而是把 `GLTFLoader` 从桩实现提升到“能加载 DamagedHelmet 并向当前工程暴露几何流和 PBR 贴图元数据”的程度，为 `REQ-019` 的 demo 场景和后续材质桥接提供输入。

## 目标

1. 引入一个轻量、vendored 的 glTF 解析库到 `infra`
2. `GLTFLoader` 能真实解析 `.gltf` 文件，并具备受控的 `.glb` 支持边界
3. 加载结果除了几何流外，还能暴露基础 PBR 材质元数据
4. 支持读取 glTF 自带的 tangent；不要求在缺失时自动生成
5. 为 DamagedHelmet 补集成测试，验证 end-to-end 加载闭环
6. glTF 第三方依赖必须遵循项目现有的“仓库内直带依赖、离线直接构建”模式

## 需求

### R1: `cgltf` 第三方集成

将 [cgltf](https://github.com/jkuhlmann/cgltf) 以 vendored 方式纳入仓库，并遵循当前项目已经形成的第三方依赖约束：

- 依赖需要直接随仓库提交
- 构建时不得依赖在线下载
- 需要的头文件必须直接在仓库内可见
- 若该库不是纯 header，则对应源文件或预编译库文件也必须直接在仓库内可见
- CMake 必须能在离线环境下直接完成配置与构建

对 `cgltf`，推荐落地形态仍然是 single-header + 一个实现宿主 cpp：

- 新增 `src/infra/external/include/cgltf/cgltf.h`
- 新增一个实现宿主 cpp，例如：
  - `src/infra/mesh_loader/cgltf_impl.cpp`

实现宿主内容仅为：

```cpp
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
```

约束：

- 不使用 git submodule
- 不使用 FetchContent
- 不要求必须复刻 `stb` 的“仅 header”形态，但必须符合项目当前统一的离线依赖模式：
  - header-only 库：头文件直接入仓
  - source 形式库：头文件 + 源文件直接入仓
  - 预编译包形式库：头文件 + 对应平台库文件直接入仓
- `cgltf` 当前首选 single-header 方案，是因为它最贴近本 REQ 的轻量目标，不是因为项目只允许 header-only 第三方库
- 若仓库还没有 `src/infra/external/README.md`，可新建并登记 cgltf 来源与 license

### R1.5: 第三方依赖接入约束

本 REQ 明确复用当前仓库已经采用的第三方依赖策略，而不是另起一套 glTF 特例。

允许的依赖接入方式：

- vendored header
- vendored source tree
- vendored 预编译 package（需同时包含头文件和对应平台库文件）

不允许的依赖接入方式：

- git submodule
- `FetchContent`
- 首次配置时联网下载源码或二进制
- 依赖系统包管理器作为唯一来源

对 `GLTFLoader` 这条需求而言，首选 `cgltf` 的原因是它可以用“头文件 + 一个实现宿主 cpp”满足上述约束，并且最小化额外构建复杂度。

### R2: `GLTFLoader` 接口扩展

扩展 [src/infra/mesh_loader/gltf_mesh_loader.hpp](../../src/infra/mesh_loader/gltf_mesh_loader.hpp)，使其除了几何流外还能暴露 PBR 元数据。

推荐接口形态：

```cpp
namespace infra {

struct GLTFPbrMaterial {
  LX_core::Vec4f baseColorFactor{1, 1, 1, 1};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  LX_core::Vec3f emissiveFactor{0, 0, 0};

  std::string baseColorTexture;
  std::string metallicRoughnessTexture;
  std::string normalTexture;
  std::string occlusionTexture;
  std::string emissiveTexture;
};

class GLTFLoader {
public:
  GLTFLoader();
  ~GLTFLoader();

  void load(const std::string& filename);

  const std::vector<LX_core::Vec3f>& getPositions() const;
  const std::vector<LX_core::Vec3f>& getNormals() const;
  const std::vector<LX_core::Vec2f>& getTexCoords() const;
  const std::vector<uint32_t>& getIndices() const;

  const std::vector<LX_core::Vec4f>& getTangents() const;
  const GLTFPbrMaterial& getMaterial() const;

private:
  struct Impl;
  Impl* pImpl;
};

} // namespace infra
```

说明：

- 本 REQ 允许继续沿用当前的 `namespace infra`
- 命名空间统一到 `LX_infra` 是合理整理项，但不作为本 REQ 的必做闭环

### R3: 最小真实解析范围

`GLTFLoader::load()` 至少要支持：

- ASCII `.gltf`
- 外部 `.bin`
- 外部贴图文件（如 `.png` / `.jpg`）
- 单 mesh / 单 primitive 的主路径

实现要求：

- 使用 `cgltf_parse_file()` + `cgltf_load_buffers()`
- 默认只消费 `data->meshes[0].primitives[0]`
- 遇到多 mesh / 多 primitive 时：
  - 不要求本 REQ 正确合并
  - 可记录 warning 后只取第一个
- 读取并导出：
  - `POSITION`
  - `NORMAL`
  - `TEXCOORD_0`
  - `TANGENT`（若存在）
  - indices

关于 `.glb`：

- 本 REQ 允许实现 `.glb` 支持
- 但首要验收闭环以 `DamagedHelmet.gltf` 为准
- 不要求为 `.glb` 单独补专门测试资产

### R4: PBR 材质元数据暴露

`GLTFLoader` 需要从 primitive 绑定的 glTF material 中提取最小 PBR 信息：

- `baseColorFactor`
- `metallicFactor`
- `roughnessFactor`
- `emissiveFactor`
- `baseColorTexture`
- `metallicRoughnessTexture`
- `normalTexture`
- `occlusionTexture`
- `emissiveTexture`

约束：

- 贴图路径以“相对 `.gltf` 所在目录”的相对路径形式暴露
- 本 REQ 只负责暴露元数据，不负责把这些纹理自动接到当前 `generic_material_loader`
- 真正的材质桥接是 `REQ-019` 或后续专门材质需求的工作

### R5: tangent 策略

本 REQ 对 tangent 的要求是“读取已有 tangent”，不是“生成 tangent”。

要求：

- 若 glTF 中有 `TANGENT` accessor，则读取到 `std::vector<LX_core::Vec4f>`
- 若 glTF 中缺失 `TANGENT`：
  - `getTangents()` 返回空 vector
  - 不在本 REQ 中做 MikkTSpace 或其他 fallback 生成

这样可以与当前工程的结构性校验要求对齐：需要 normal map 的 shader path 可以在更高层决定是否拒绝缺 tangent 的 mesh。

### R6: 错误处理

以下情况必须抛 `std::runtime_error`：

- 文件不存在
- `cgltf_parse_file()` 或 `cgltf_load_buffers()` 失败
- 缺少必需 attribute（至少 `POSITION`）
- primitive 不是三角形
- index 类型不支持
- 使用 data URI / base64 内嵌图像而本实现不支持时

错误信息约束：

- message 中带上文件路径
- 尽量带上 cgltf 错误码或可读描述

多 mesh / 多 primitive 的处理策略：

- 记录 warning
- 仅加载第一个
- 不在本 REQ 中视为硬错误

### R7: 集成测试

新增 `src/test/integration/test_gltf_loader.cpp`，至少覆盖：

- `loads_damaged_helmet`
  - `cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")`
  - 调用 `load("assets/models/damaged_helmet/DamagedHelmet.gltf")`
  - 断言 `positions` / `normals` / `texCoords` / `indices` 非空
  - 若 DamagedHelmet 带 tangent，则断言 `tangents` 非空
  - 断言材质贴图路径字段至少包含 baseColor / metallicRoughness / normal / occlusion / emissive 的预期非空项
- `throws_on_missing_file`
  - 不存在路径抛异常
- `throws_on_corrupt_file`
  - 传入一个非 glTF 文件，如 `assets/models/viking_room/viking_room.obj`
  - 应抛异常

测试约束：

- 测试注册点是 [src/test/CMakeLists.txt](../../src/test/CMakeLists.txt)
- 不要求验证渲染结果
- 不应把具体贴图文件名写死到过于脆弱的程度；优先断言“非空 + 后缀/关键字段合理”

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/external/include/cgltf/cgltf.h` | 新增 vendored header |
| `src/infra/external/README.md` | 新增或补 cgltf 条目 |
| `src/infra/mesh_loader/gltf_mesh_loader.hpp` | 扩展接口，补 tangent 与 PBR 元数据 |
| `src/infra/mesh_loader/gltf_mesh_loader.cpp` | 从桩实现改为真实解析实现 |
| `src/infra/mesh_loader/cgltf_impl.cpp` | 新增 `CGLTF_IMPLEMENTATION` 宿主 |
| `src/infra/CMakeLists.txt` | 将 `cgltf_impl.cpp` 纳入 `INFRA_SOURCES` |
| `src/test/integration/test_gltf_loader.cpp` | 新增 |
| `src/test/CMakeLists.txt` | 注册新测试 |
| 相关说明文档 | 在本 REQ 完成后同步修正“GLTF 已支持 PBR 元数据”的错误现状描述 |

## 边界与约束

- 不做节点层级、动画、skin、morph target
- 不做多 mesh / 多 primitive 合并
- 不做完整 glTF scene graph 导入
- 不做 tangent 自动生成
- 不做 data URI 图像解析
- 不把 glTF material 自动桥接到当前材质系统
- 不把命名空间统一整理作为主闭环阻塞项
- 不为 glTF loader 单独引入任何需要联网下载的第三方依赖流程

## 依赖

- `REQ-010`：提供 DamagedHelmet 资产与 `cdToWhereAssetsExist()`

## 下游

- `REQ-019`：demo_scene_viewer 读取 DamagedHelmet，并把 `GLTFLoader` 输出桥接到当前材质/mesh 路径
- 后续材质桥接需求：将 `GLTFPbrMaterial` 转成工程内 `MaterialInstance`
- Phase 3 资产管线：复用 `GLTFLoader` 作为基础导入入口

## 实施状态

2026-04-17 已落地（对应 OpenSpec change `gltf-pbr-loader`）：

- `cgltf` v1.15 以 vendored single-header 方式纳入 `src/infra/external/include/cgltf/cgltf.h`；`src/infra/external/README.md` 登记全部 vendored 依赖（包括 cgltf、stb、tinyobjloader、imgui、SDL3、SPIRV-Cross、yaml-cpp）的 shape / upstream / version / license / consumers；构建流程无任何联网步骤
- `src/infra/mesh_loader/cgltf_impl.cpp` 作为 `CGLTF_IMPLEMENTATION` 唯一宿主，加入 `src/infra/CMakeLists.txt` 的 `INFRA_SOURCES`
- `src/infra/mesh_loader/gltf_mesh_loader.hpp` 新增 `GLTFPbrMaterial` 结构（baseColor/metallic/roughness/emissive factors + 5 张贴图 URI）、`getTangents()`、`getMaterial()`；PImpl 隔离，header 不引入 cgltf
- `gltf_mesh_loader.cpp` 用 `cgltf_parse_file` + `cgltf_load_buffers` 实现真实解析；消费 `meshes[0].primitives[0]`；POSITION 必需，NORMAL/TEXCOORD_0/TANGENT 可选；index 支持 u8/u16/u32 widen 至 uint32_t；primitive 非三角形、data URI / buffer_view inline 图像、解析失败均抛 `std::runtime_error`（message 带文件路径与 cgltf 错误码可读化）；多 mesh / 多 primitive 打 warning 取 [0]
- 贴图 URI 以相对 `.gltf` 所在目录形式存储，不做 filesystem resolve
- `src/test/integration/test_gltf_loader.cpp` 新增 3 用例（`loads_damaged_helmet` / `throws_on_missing_file` / `throws_on_corrupt_file`）并注册到 `src/test/CMakeLists.txt`，本地 PASS
- 注：本仓库 DamagedHelmet.gltf 未声明 TANGENT accessor，集成测试据此断言 `getTangents()` 为空且 loader 不抛；材质桥接到 `MaterialInstance` 留给 REQ-019 / 后续材质需求
