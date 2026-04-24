# Phase 3 · 资产管线

> **目标**：让资产（网格 / 纹理 / 材质 / 场景 / shader）**按稳定 ID 被引用**，能从外部文件加载、能保存到文件、能热重载，并且**每份资产都记得自己的来历**。
>
> **依赖**：Phase 2 的 `SceneNode` 树形结构 + 命令层 + 事件流。
>
> **可交付**：`demo_scene_save_load` — 在运行期编辑场景（移动物体、换材质），保存到文件，重启后状态恢复。

**本阶段落实的原则**：[P-2 事件流](principles.md#p-2-状态即事件流) · [P-10 Provenance](principles.md#p-10-资产血统--provenance) · [P-15 版本化](principles.md#p-15-重构友好--版本化的一切)

## 范围与边界

**做**：
- 稳定资产 ID（全局唯一，跨进程跨会话稳定）
- 资产注册表（按 ID 查，按 path 反查）
- 引用计数 + 生命周期
- 内容寻址（content hash）：同样内容 = 同一份 GPU 资源
- 资产血统 / provenance 元数据
- Scene 序列化（文本为主，二进制为辅）
- 热重载（shader / texture / mesh / material）
- 资产导入器骨架：各种外部格式走统一入口
- Schema 版本化 + 透明迁移

**不做**：
- 资产 cooking / 压缩（→ Phase 12）
- 多线程加载（引入 job system 太重，留给 Phase 6 之后）

---

## 前置条件

- Phase 2 完成：`SceneNode` 树形结构 + `Transform`
- 可选：Phase 1 完成，这样序列化 scene 时能包含更多资源类型（环境贴图 / 阴影参数）

---

## 工作分解

### REQ-301 · Asset GUID

```cpp
struct AssetGuid {
    uint64_t high;
    uint64_t low;

    static AssetGuid generate();   // UUID v4
    std::string toString() const;  // "01234567-89ab-..."
    static std::optional<AssetGuid> parse(std::string_view);

    bool operator==(const AssetGuid&) const = default;
    struct Hash { size_t operator()(const AssetGuid&) const; };
};
```

- 生成使用 `std::random_device` + v4 规则
- 序列化格式：标准 8-4-4-4-12 带连字符

**验收**：`test_asset_guid.cpp`：生成 10000 个 GUID 无重复，parse/toString 往返。

### REQ-302 · AssetHandle<T>

强类型 handle，不是裸 GUID：

```cpp
template <typename T>
class AssetHandle {
public:
    AssetGuid guid;

    std::shared_ptr<T> load() const;    // 走 AssetRegistry
    bool isLoaded() const;

    operator bool() const { return guid != AssetGuid{}; }
};

using TextureHandle  = AssetHandle<Texture>;
using MeshHandle     = AssetHandle<Mesh>;
using MaterialHandle = AssetHandle<MaterialInstance>;
using SceneHandle    = AssetHandle<Scene>;
```

- 可以从 JSON 读取：JSON 里存的是 GUID 字符串
- `load()` 是懒加载：第一次调用去 registry 找 / 加载，后续直接命中 cache

**验收**：`Mesh` 的引用在 `MaterialInstance` 里全部改成 `MeshHandle`，构造和序列化往返一致。

### REQ-303 · AssetRegistry

```cpp
class AssetRegistry {
public:
    template <typename T>
    std::shared_ptr<T> get(AssetGuid guid);

    template <typename T>
    std::shared_ptr<T> getByPath(const std::filesystem::path& relPath);

    // 在引擎启动时扫描 assets/ 目录构建 path → GUID 索引
    void scan(const std::filesystem::path& root);

    // 注册一个加载器
    template <typename T>
    void registerLoader(std::function<std::shared_ptr<T>(std::filesystem::path)> loader);

private:
    std::unordered_map<AssetGuid, std::any> m_cache;  // 或按类型分开的 map
    std::unordered_map<std::filesystem::path, AssetGuid> m_pathIndex;
};
```

- 每个资产文件配一个 `.meta` 文件存 GUID（避免文件移动时 GUID 丢失）
- `.meta` 格式：`{"guid": "xxx"}`
- `scan()` 遇到没有 `.meta` 的资产自动生成一个

**验收**：`test_asset_registry.cpp`：把两个 `.obj` 文件放到 assets 下，scan 后按 path 和 guid 都能查到，生成的 `.meta` 持久化。

### REQ-304 · Resource 的 GUID 化

- `Mesh`、`Texture`、`MaterialInstance`、`CubemapResource` 等资源类都加 `m_guid` 字段
- 现有 loader（`blinnphong_material_loader.cpp` / `pbr_cube_material_loader.cpp`）要在构造时从 registry 读 GUID，而不是每次都生成新的
- `RenderableSubMesh` 持有 `MeshHandle` + `MaterialHandle`，而不是裸 `MeshSharedPtr` / `MaterialInstanceSharedPtr`

**验收**：同一个 `.obj` 被加载 5 次只有一份 GPU buffer。

### REQ-305 · Scene 序列化

选 **JSON** 作为主格式，二进制 baked 留给 Phase 9。

- 选 `nlohmann/json` 作为库（header-only、被广泛使用、已有项目可能已经间接引入）
- `SceneNode` 导出为：
  ```json
  {
    "name": "root",
    "transform": {"pos": [0,0,0], "rot": [0,0,0,1], "scale": [1,1,1]},
    "renderable": {"mesh": "guid-...", "material": "guid-..."},
    "camera": null,
    "light": null,
    "children": [ ... ]
  }
  ```
- Scene 本体有 `save(path)` / `load(path)` 方法
- load 时所有 GUID 引用通过 registry 懒加载

**验收**：`demo_scene_save_load` — 运行期用自由飞行相机飞到任意位置，按 S 保存，重启后还在那个位置。

### REQ-306 · MaterialInstance 序列化

材质实例的状态 = `(template, ubo 字节 buffer, 纹理列表)`。序列化策略：

- Material template 按 GUID 引用
- UBO 状态导出成 `{memberName: value}` 的 JSON 对象
- 纹理按 binding name → GUID 映射

```json
{
  "template": "guid-pbr_cube",
  "uniforms": {
    "baseColor": [0.8, 0.15, 0.15],
    "metallic": 0.0,
    "roughness": 0.35,
    "ao": 1.0
  },
  "textures": {}
}
```

**验收**：构造一个 PBR material instance → 改 baseColor → 保存 JSON → 重建 instance → 字节比较 UBO buffer 一致。

### REQ-307 · Shader 热重载

- `CompiledShader` 记住自己的源文件路径 + 上次 mtime
- 一个独立线程或主循环 poll 检查 mtime 变化
- mtime 变化 → 重新编译 + 反射 → 替换 `CompiledShader` 内容
- 所有依赖的 pipeline 被标记失效 → `PipelineCache` 重建

**验收**：运行期修改 `pbr_cube.frag` 保存，不重启就能看到新效果（或编译错误日志）。

### REQ-308 · Texture / Mesh 热重载

- 走和 shader 一样的 mtime watch 机制
- Texture 替换后所有引用自动指向新 GPU texture（因为走的是 `shared_ptr`）
- Mesh 替换后所有 `RenderableSubMesh` 的 vertex/index buffer 刷新

**验收**：修改一张 PNG 保存，游戏里对应物体的贴图立即更新。

### REQ-309 · 统一的资产导入入口

- 一个函数族：`importAsset<T>(source)`，按来源类型分发到具体的 loader
- 未来新增格式只需注册新分发规则
- 现有的各类 loader 被 wrap 到这个入口里

**验收**：`registry.scan(root)` 自动识别已支持的所有格式，创建正确类型的 handle。

### REQ-310 · 资产 Provenance

契合 [P-10](principles.md#p-10-资产血统--provenance)：

每份资产携带 `provenance` 元数据，描述"它是怎么来的"：

- **kind**：`imported` / `generated` / `procedural` / `edited`
- **source**：`imported` 时是源文件 path；`generated` 时是 prompt + generator id；`procedural` 时是代码入口
- **created_at** / **author**（人类或 agent）
- **cost**（如果是生成型，记录 token / 时间 / USD，契合 [P-9](principles.md#p-9-成本模型是一等公民)）
- **reproducible**：布尔，标记是否能重生一份等价产物
- **edits**：后续人工编辑链（改 prompt / 手动 uv 编辑 / 替换某一层贴图）

这一字段在**所有**资产上都存在，不仅是 AI 生成资产。手动导入资产也要记录 source path + import timestamp，让"这个资产哪来的？" 永远有答案。

**验收**：`query.assets.provenance(handle)` 返回结构化来源信息；可以按 provenance 字段过滤：`query.assets.find({generated_by:"text_to_image", unused:true})`。

### REQ-311 · Schema 版本化 + 迁移链

契合 [P-15](principles.md#p-15-重构友好--版本化的一切)：

- 每个资产元数据文件 / 场景文件 / prefab 文件都带 `schema_version`
- 引擎内置一个按 (type, version) 注册的迁移函数链
- 加载旧版本数据时自动逐级向上迁移
- 保存时用最新版本
- 迁移失败产生结构化错误（[P-12](principles.md#p-12-错误即教学)），带 `fix_hint` 指向修复命令

**迁移函数的形态（抽象）**：

```
registerMigration(type="Scene", from_version=3, to_version=4, fn)
```

**验收**：用旧版本引擎产出的场景文件能在新版本加载而无需手工干预。

### REQ-312 · 内容寻址

契合**去重**与 [P-1 确定性](principles.md#p-1-确定性是架构级不变量)：

- 每份资产除了稳定 ID 之外还计算**内容哈希**（SHA-256 或同级）
- 注册表同时维护 `id → asset` 和 `content_hash → asset`
- 加载同样内容的资产时（即便 ID 不同）直接复用已有 GPU 资源
- Provenance 上报"reproducible"检查依赖这个

**验收**：把同一张 PNG 复制成两个文件、分配两个 ID、加载时共享一份 GPU texture。

### REQ-313 · 资产相关的命令与事件

本阶段所有写入操作（import / unload / hot-reload）都表达为 Phase 2 REQ-213 的命令 + REQ-215 的事件：

- `assets.import(source)` / `assets.unload(handle)` / `assets.reload(handle)`
- 加载完成 → 产生 `AssetLoadedEvent`
- 热重载 → 产生 `AssetReplacedEvent`

这一条让编辑器 / agent / 脚本层能**订阅**资产变化 —— 新增一张贴图时，inspector 的 asset picker 自动看到它。契合 [P-19 命令总线](principles.md#p-19-bi-directional-命令总线)。

**验收**：订阅事件流后，从外部拖一个文件进 `assets/` 目录 → 自动看到 `AssetLoadedEvent` 到达。

---

## 里程碑

### M3.1 · GUID + Registry 就位

- REQ-301 + REQ-302 + REQ-303 + REQ-304 完成
- demo：同一个 mesh 在多个 renderable 共享

### M3.2 · Scene 可持久化

- REQ-305 + REQ-306 完成
- demo：`demo_scene_save_load` 跑通往返

### M3.3 · 热重载

- REQ-307 + REQ-308 + REQ-309 完成
- demo：改 shader / 贴图不重启即刻生效

### M3.4 · Provenance + 版本化 + 内容寻址

- REQ-310 + REQ-311 + REQ-312 + REQ-313 完成
- demo：AI 生成一份资产 → 查询 provenance 能看到 prompt；老版本场景文件透明迁移；重复内容去重

---

## 风险 / 未知

- **循环引用 / 生命周期**：Scene → Renderable → Material → Texture 是一条有向链，没问题。但如果未来 Script 组件持有 SceneHandle，就会回指。用 weak_ptr 打破。
- **JSON 性能**：scene 文件 > 10 MB 时解析会慢。解决：Phase 9 做二进制 baked 格式。
- **GUID 冲突**：v4 UUID 128 位碰撞概率可忽略。
- **`std::any` 类型擦除开销**：`AssetRegistry::m_cache` 用 `unordered_map<AssetGuid, std::any>` 查询有开销。如果 profiling 显示问题，改成"每种类型一个 map"。
- **`.meta` 文件丢失**：用户复制资产但没复制 meta。解决：auto-regenerate + 日志警告。

---

## 与现有架构的契合

- 已有的材质 loader 是典型的 factory function，可以被包成注册表的 loader 而不需要大改。
- `IGpuResource` 的 dirty 通道和 GPU sync 路径完全不受影响——资产系统是它**上面**的一层管理。
- `StringID` 等 intern 系统和稳定资产 ID 是两套东西：`StringID` 是"引擎进程内的 uniquename"，资产 ID 是"跨进程、跨会话的身份"。两者正交共存。
- Shader 热重载触发的 `PipelineCache` 重建路径已经存在（cache miss 会重建），只需要外部显式 invalidate 一次。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-2 事件流](principles.md#p-2-状态即事件流) | REQ-313 所有资产变化都是事件 |
| [P-10 Provenance](principles.md#p-10-资产血统--provenance) | REQ-310 为每份资产记录来源 / 成本 / 编辑历史 |
| [P-15 版本化](principles.md#p-15-重构友好--版本化的一切) | REQ-311 所有持久化格式带 schema version + 迁移链 |
| [P-19 命令总线](principles.md#p-19-bi-directional-命令总线) | REQ-313 资产操作全部通过命令总线 |

---

## 下一步

Phase 3 完成后进入 [Phase 4 动画](phase-4-animation.md) 或 [Phase 5 物理](phase-5-physics.md)。两者都需要 Phase 2 的 Transform 和 Phase 3 的资产管线（动画 clip 是 asset，物理 shape 可以是 asset）。
