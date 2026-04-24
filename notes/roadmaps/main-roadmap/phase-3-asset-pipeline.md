# Phase 3 · 资产管线

> **目标**：资产从“硬编码路径 + `cdToWhereAssetsExist` 启动期 cwd 校准”升级到“GUID + 序列化 + 热重载”。脱离引擎目录结构的游戏资产能被工具产出、被引擎稳定加载。
>
> **依赖**：Phase 2（`Transform` / 场景层级 / dump API）。
>
> **可交付**：
> - `engine-cli save-scene` / `engine-cli load-scene`
> - `engine-cli watch-asset` 改贴图 / `.material` / 场景 JSON 热更新
> - Assets 按 GUID 寻址，`.meta` 文件持久化

## 当前实施状态（2026-04-24）

**未开工**。前置的加载器已存在，但 GUID / 序列化 / 热重载全都没有。

| 条目 | 状态 | 备注 / 位置 |
|------|------|-------------|
| 资产目录约定 | ✅ | `openspec/specs/asset-directory-convention/spec.md` + `assets/` 目录 |
| 启动期 cwd 校准 | ✅ | `cdToWhereAssetsExist(...)` (`src/core/utils/filesystem_tools.*`) |
| OBJ / GLTF mesh loader | ✅ | `src/infra/mesh_loader/*` |
| Texture loader + placeholder | ✅ | `src/infra/texture_loader/*` |
| 通用 `.material` YAML loader | ✅ | `loadGenericMaterial(...)` |
| Asset GUID / registry | ❌ | REQ-301 / REQ-302 |
| 文件→GUID 索引持久化（`.meta`） | ❌ | REQ-303 |
| Resource GUID 化（Mesh / Texture / Material / Cubemap） | ❌ | REQ-304 |
| Scene 序列化（JSON） | ❌ | REQ-305 |
| 显式 runtime asset root contract | ❌ | REQ-306 |
| Shader 热重载 | ❌ | REQ-307 |
| Texture / Mesh 热重载 | ❌ | REQ-308 |
| 统一资产导入入口 `importAsset<T>` | ❌ | REQ-309 |

## 范围与边界

**做**：

- GUID 生成（UUID v4）+ registry（name/path/guid 双向映射）
- `.meta` sidecar 持久化
- 显式 runtime asset root 契约（取代 cwd 启发式）
- Scene 序列化 / 反序列化（JSON 为主，二进制 baked 留给 Phase 12）
- Mesh / Texture / Material / Cubemap 热重载
- 统一资产导入入口 `importAsset<T>(source)` 按类型分发

**不做**：

- 资产数据库 UI（→ Phase 9 编辑器）
- 压缩 / 打包格式（→ Phase 12）
- 网络资产（云 CDN / 云同步）

## 前置条件

- Phase 2 的 `Transform` 层级（序列化需要稳定的 scene shape）
- Phase 2 的 `dumpScene` API（反序列化的对照）

## 工作分解

### REQ-301 · AssetGuid + 生成

- `AssetGuid` 类型（128-bit UUID）
- 生成函数 `AssetGuid::newRandom()` + 解析 `AssetGuid::parse(str)` + 字符串化 `toString()`
- 作为 `ResourceSharedPtr` 的替代 handle 类型

**验收**：随机生成 1M 个 GUID 无冲突。

### REQ-302 · AssetRegistry

- 进程级单例 / 显式持有的 `AssetRegistry`
- `registerAsset(guid, path)` / `findByGuid(guid)` / `findByPath(path)`
- 支持 `scan(rootDir)` 递归发现已有 `.meta`

**验收**：`test_asset_registry.cpp`：把两个 `.obj` 放到 assets 下，scan 后按 path 与 guid 都能查到。

### REQ-303 · `.meta` Sidecar 持久化

- 每个 asset 旁边写一个 `<asset>.meta`：
  ```yaml
  guid: 9b4e0c8a-...
  type: mesh
  version: 1
  importSettings:
    smoothNormals: true
  ```
- 加载时若 `.meta` 存在 → 读 GUID；不存在 → 生成新 GUID + 写 `.meta`

**验收**：删 `.meta` 后重启，新 `.meta` 生成但内容与原始等价（GUID 稳定 = 新生；引用仍按 path 回填）。

### REQ-304 · Resource GUID 化

- `Mesh` / `Texture` / `MaterialInstance` / `CubemapResource` 等资源类加 `m_guid` 字段
- 现有 loader（`generic_material_loader.cpp` 等）构造时从 registry 读 GUID
- `SceneNode` 持有 `MeshHandle` + `MaterialHandle`，而非裸 `MeshSharedPtr` / `MaterialInstanceSharedPtr`
- Handle 与 shared_ptr 双通道并存：handle 是序列化身份，shared_ptr 是运行期引用

**验收**：同一 `.obj` 加载 5 次只有一份 GPU buffer。

### REQ-305 · Scene 序列化（JSON）

选型：`nlohmann/json`（header-only，生态广）。

- `SceneNode` 导出：
  ```json
  {
    "name": "player",
    "transform": { "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1] },
    "mesh": "guid:9b4e0c8a-...",
    "material": "guid:7d3a1234-...",
    "children": [ ... ]
  }
  ```
- `Scene::toJson()` / `Scene::fromJson(...)`
- 反序列化走 AssetRegistry 把 handle 还原为 shared_ptr

**验收**：save → load round-trip，`dumpScene(Full)` 前后完全一致。

### REQ-306 · 显式 runtime asset root

- 取代 `cdToWhereAssetsExist` 启发式（保留作 dev-only fallback）
- 程序启动时指定 `--asset-root <path>`（CLI 或 env）
- `AssetRegistry` 不再依赖 cwd

**验收**：把可执行放到任意目录 + 显式指定 root，能正常加载资产。

### REQ-307 · Shader 热重载

- mtime watch `assets/shaders/glsl/*.{vert,frag,glsl}`
- 改动 → 重新编译 → reflect → 如果反射结构不变，就替换 `IShader::shader_module`；变则触发 pipeline 重建

**验收**：运行时改 shader 保存，画面在下一帧切换。

### REQ-308 · Texture / Mesh 热重载

- 与 shader 同机制：mtime watch
- Texture 替换：走 `shared_ptr` 引用链自动替换 GPU texture
- Mesh 替换：所有 `SceneNode` 的 vertex / index buffer 刷新

**验收**：改 PNG 保存，场景里对应贴图立即更新。

### REQ-309 · 统一资产导入入口

- `importAsset<T>(source)` 按来源类型分发到具体 loader：`.obj` / `.gltf` / `.hdr` / `.material` / ...
- 分发规则注册式，新增格式只需加注册
- 现有 loader wrap 到此入口

**验收**：同一函数签名同时能导入 mesh / texture / material / cubemap。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M3.1 · GUID + 注册 | REQ-301 + REQ-302 + REQ-303 | scan 后按 path / guid 都能查 |
| M3.2 · Resource GUID 化 | REQ-304 | 同资产加载去重 |
| M3.3 · 场景序列化 | REQ-305 + REQ-306 | save / load round-trip |
| M3.4 · 热重载 | REQ-307 + REQ-308 | shader / texture / mesh 改动立即生效 |
| M3.5 · 统一导入 | REQ-309 | `importAsset<T>` 替代多入口 |

## 风险 / 未知

- **`.meta` 冲突**：多人协作场景下 `.meta` 的 merge conflict。Unity 的长期解决方案是“`.meta` 写入 git”。本引擎保持一致。
- **Handle 生命周期**：handle → shared_ptr 还原时资源可能已被卸载。需要 registry 持有 weak_ptr 并懒加载。
- **Hot reload 与 pipeline cache**：shader 改签名时 pipeline 需重建；未绑定的其他 shader 不能被误连累。
- **JSON 性能**：大场景序列化慢。保留二进制 baked 为 Phase 12 选项。

## 与 AI-Native 原则契合

- [P-2 事件流](principles.md#p-2-状态即事件流)：资产加载 / 卸载 / 替换都发出事件；agent 可订阅 diff。
- [P-10 Provenance](principles.md#p-10-资产血统--provenance)：`.meta` 是 provenance 的持久化形态。
- [P-15 版本化](principles.md#p-15-重构友好--版本化)：`.meta` 带 `version` 字段；升级提供迁移链。
- [P-18 沙箱进程](principles.md#p-18-沙箱友好的进程模型)：显式 asset root 是沙箱会话的前置。

## 与现有架构契合

- `generic_material_loader.cpp` 已是统一 material 入口；GUID 化是在其基础上加 handle 层。
- `SceneNode` 持有 `MeshSharedPtr` / `MaterialInstanceSharedPtr`，引入 handle 时改成两者并存过渡。
- `cdToWhereAssetsExist` 作为 dev fallback 继续保留；正式入口变成显式 root。

## 下一步

- [Phase 4 · 动画](phase-4-animation.md)（需要 Phase 3 完成 animation clip 资产的加载）
- [Phase 5 · 物理](phase-5-physics.md)（需要 Phase 3 完成 collision shape / mesh 资产的加载）
- [Phase 8 · Vue UI 容器](phase-8-web-ui.md)（需要 Phase 3 完成 UI 资产加载路径）
