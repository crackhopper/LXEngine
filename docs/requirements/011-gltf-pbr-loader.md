# REQ-011: cgltf 集成 + GLTFLoader 支持 PBR 通道

## 背景

`src/infra/mesh_loader/gltf_mesh_loader.cpp:35-40` 当前的 `GLTFLoader::load` 是一个**纯桩实现**：

```cpp
if (magic == 0x46546C67) {
  throw std::runtime_error("Binary GLTF (.glb) not yet supported - use cgltf library");
}
throw std::runtime_error("ASCII GLTF (.gltf) not yet supported - use cgltf library");
```

`src/infra/mesh_loader/gltf_mesh_loader.hpp:8-23` 的接口也只暴露 positions / normals / texCoords / indices —— **没有 PBR 材质通道、没有 tangent、没有多 mesh、没有节点层级**。这意味着：

- REQ-010 引入的 DamagedHelmet（PBR 全通道 glTF）目前**完全无法加载**
- Phase 1 的 PBR / IBL / shadow 调试链路在材质侧没有真实输入数据
- 后续 Phase 3 的资产管线没有可用的 glTF 入口

`src/infra/external/include/` 当前包含 `stb` + `tinyobjloader`，**没有** glTF 解析库。本需求引入 [cgltf](https://github.com/jkuhlmann/cgltf)（单 header，MIT，stb 风格）作为第三方依赖，并把 `GLTFLoader` 从桩实现提升为支持 PBR 通道的真实加载器。

本需求**不**做：节点层级 / 多 mesh 切分（一个 gltf 文件按 primitive 数量返回多个 mesh 而不是合并）/ 动画 / skin —— 这些留给 Phase 3 资产管线或后续 REQ。本 REQ 的目标是"让 DamagedHelmet 可以被加载并送到 PBR shader"。

## 目标

1. 引入 `cgltf` 单 header 到 `src/infra/external/include/cgltf/`
2. `GLTFLoader` 真正解析 ascii `.gltf` 与 binary `.glb`，至少支持单 mesh 单 primitive 路径
3. 加载结果暴露 PBR material 元数据（baseColor factor + 5 个贴图路径）
4. 加载结果包含 tangent（来自 glTF accessor 或 MikkTSpace fallback）
5. 集成测试用 REQ-010 的 DamagedHelmet 验证 end-to-end

## 需求

### R1: cgltf 第三方集成

- 把 `cgltf.h`（单文件，~10k 行）放入 `src/infra/external/include/cgltf/cgltf.h`
- 在 `src/infra/mesh_loader/gltf/` 下新建一个 `cgltf_impl.cpp`，仅包含：

```cpp
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
```

- 不通过 git submodule、不通过 FetchContent，与现有 stb / tinyobjloader 保持一致
- 在 `src/infra/external/README.md`（若没有则新建）登记一行：`cgltf MIT, https://github.com/jkuhlmann/cgltf, vendored at ...`

### R2: `GLTFLoader` 接口扩展

替换 `src/infra/mesh_loader/gltf_mesh_loader.hpp` 的接口：

```cpp
namespace infra {

struct GLTFPbrMaterial {
  // factors（无贴图时使用）
  LX_core::Vec4f baseColorFactor{1, 1, 1, 1};
  float          metallicFactor = 1.0f;
  float          roughnessFactor = 1.0f;
  LX_core::Vec3f emissiveFactor{0, 0, 0};

  // 贴图相对路径（相对 .gltf 所在目录），空字符串表示无该贴图
  std::string baseColorTexture;
  std::string metallicRoughnessTexture;  // glTF 约定：G=roughness, B=metallic
  std::string normalTexture;
  std::string occlusionTexture;
  std::string emissiveTexture;
};

class GLTFLoader {
public:
  GLTFLoader();
  ~GLTFLoader();

  /// 加载 .gltf (ascii + 外部 .bin) 或 .glb (binary 单文件)。
  /// 失败抛 std::runtime_error，message 带文件路径与 cgltf 原始错误码。
  void load(const std::string &filename);

  // 顶点流（与现状兼容）
  const std::vector<LX_core::Vec3f> &getPositions() const;
  const std::vector<LX_core::Vec3f> &getNormals() const;
  const std::vector<LX_core::Vec2f> &getTexCoords() const;
  const std::vector<uint32_t>       &getIndices() const;

  // 新增：tangent（4 分量，w 为 bitangent 符号，符合 glTF 约定）
  const std::vector<LX_core::Vec4f> &getTangents() const;

  // 新增：PBR 材质元数据
  const GLTFPbrMaterial &getMaterial() const;

private:
  struct Impl;
  Impl *pImpl;
};

} // namespace infra
```

实现要点（`gltf.cpp`）：

- 用 `cgltf_parse_file` + `cgltf_load_buffers` 读 glTF
- 仅取 `data->meshes[0].primitives[0]`（多 mesh / 多 primitive 留给后续 REQ；遇到时 log warning 取第一个）
- accessor 读 POSITION / NORMAL / TEXCOORD_0 / TANGENT / indices，做 little-endian 拷贝到 `std::vector`
- 若 TANGENT accessor 缺失：本 REQ **暂不**生成（接口返回空 vector），上层调用方需要 tangent 时报错；MikkTSpace fallback 留给下游 REQ
- 材质：从 `primitive->material->pbr_metallic_roughness` 读 factor 与 texture index，把 texture image 的 uri 解析为相对路径
- 释放 cgltf 资源在 `Impl` 析构

### R3: 错误处理

- 文件不存在 / cgltf 返回错误 / 必需 attribute (POSITION) 缺失 / index 类型不支持（非 u16/u32）→ 抛 `std::runtime_error`，message 形如：`"GLTFLoader::load(<path>): cgltf error <code>: <description>"`
- 多 mesh / 多 primitive：取第一个并 `std::cerr` 输出一条 warning，不抛
- 三角化：本 REQ 假设输入已三角化（DamagedHelmet 满足）；遇到非三角形 primitive 抛错
- 贴图 uri 是 data URI（base64 内联）：本 REQ **不支持**，抛错并提示用户改成外部文件 —— 简化实现，DamagedHelmet 用的是外部 png

### R4: 命名空间收敛

`gltf.hpp` 当前在 `namespace infra`，与项目其他 infra 用 `namespace LX_infra`（见 `src/infra/material_loader/blinn_phong_material_loader.hpp`）不一致。本 REQ 顺手把它迁到 `namespace LX_infra`，并更新所有 caller。

注意：这是**唯一**与本 REQ 主题不直接相关的改动，保留是因为它和 `gltf.hpp` 改动无法分离。

## 测试

- **单元/集成测试** `src/test/integration/test_gltf_loader.cpp`（新增）：
  - `loads_damaged_helmet`：调用 `cdToWhereAssetsExist("models/damaged_helmet/DamagedHelmet.gltf")`，加载，断言 positions / normals / texCoords / tangents / indices 非空，断言 `getMaterial().baseColorTexture` 以 `Default_albedo.jpg`/`.png` 结尾，metallicRoughness / normal / occlusion / emissive 路径同理
  - `throws_on_missing_file`：传一个不存在的路径，期望 `std::runtime_error`
  - `throws_on_corrupt_file`：传一个非 glTF 文件（指向 `viking_room.obj`），期望 `std::runtime_error`
- 测试不验证渲染结果，那是 REQ-019 的事

## 修改范围

| 文件 | 改动 |
|---|---|
| `src/infra/external/include/cgltf/cgltf.h` | 新增（vendored single-header） |
| `src/infra/external/README.md` | 新增 cgltf 条目 |
| `src/infra/mesh_loader/gltf_mesh_loader.hpp` | 重写接口（R2） |
| `src/infra/mesh_loader/gltf_mesh_loader.cpp` | 重写实现（R1+R2+R3） |
| `src/infra/mesh_loader/gltf/cgltf_impl.cpp` | 新增（CGLTF_IMPLEMENTATION 宿主） |
| `src/infra/mesh_loader/CMakeLists.txt` | 把 `cgltf_impl.cpp` 加入 sources、添加 `external/include/cgltf` 到 include path |
| `src/test/integration/test_gltf_loader.cpp` | 新增 |
| `src/test/integration/CMakeLists.txt` | 注册新测试 |
| 命名空间 `infra` → `LX_infra`（含 hpp/cpp 与所有 caller） | R4 |

## 边界与约束

- **不做**：节点层级、多 mesh、多 primitive 合并、skin、animation、morph target、KHR 扩展（如 `KHR_materials_clearcoat` / `KHR_materials_transmission`）
- **不做**：MikkTSpace tangent 生成 —— 缺 tangent 的 mesh 只能用不需要 normal map 的 shader path
- **不做**：把 glTF material 自动适配到现有 `BlinnPhongMaterialLoader` —— 那是 REQ-019 在加载 DamagedHelmet 时的整合工作，本 REQ 只暴露元数据
- **不做**：image 内嵌 (data URI) 解析
- 性能：DamagedHelmet（~3 MB，~46k 三角形）加载时间 ≤ 100 ms 在 release build 下

## 依赖

- **REQ-010**（必需）：测试 fixture DamagedHelmet 由 REQ-010 引入到 `assets/models/damaged_helmet/`

## 下游

- **REQ-019** demo_scene_viewer：加载 DamagedHelmet 渲染
- **未来 PBR material loader REQ**（暂未起编号）：把 `GLTFPbrMaterial` 桥接到引擎内 `MaterialInstance`
- **Phase 3 资产管线**：`GLTFLoader` 是 Phase 3 的关键复用点

## 实施状态

未开始。
