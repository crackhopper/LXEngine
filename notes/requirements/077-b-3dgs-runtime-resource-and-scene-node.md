# REQ-077-b: 3DGS Runtime Resource And Scene Node

> 2026-06-14 重排：本需求原为 `REQ-062-a`，现按当前主线后置到 `REQ-077-b`。当前 scene document 仍只有 `mesh.uri` / `material.uri` 等表面，没有 `gaussianSplat.uri`、`GaussianSplatComponent` 或等价 runtime object。

## 背景

3DGS 是点状体渲染表示，不是 triangle mesh。当前 scene 文档只有 `mesh.uri`，运行时也只会构建 `Mesh` + `MaterialInstance`。为了支持 `.ply`，需要一个明确的 `GaussianSplatComponent` 或等价运行时对象，并让 scene 能把 `.ply` URI 映射到该对象。

当前代码对照见 `notes/roadmaps/research/3dgs-ply-rendering/03-LX当前状态对照.md`。当前 `SceneNodeDocument` 仍以 `meshUri` 为主，`SceneRuntime` 的 mesh 分支只处理内置 mesh、glTF、OBJ/project asset 等三角网格路径；本 REQ 的目标是引入明确的 3DGS 节点，而不是让 `.ply` 伪装成普通 `mesh.uri`。

## 目标

1. 支持 scene 中引用 3DGS PLY。
2. 建立 `GaussianSplatCloud` 的 runtime ownership。
3. 让 editor scene tree 能显示 3DGS 节点。
4. 保持普通 mesh path 不受影响。

## 需求

### R1: Scene 文档表面

Scene SHALL 支持 3DGS 节点。canonical 表面：

```yaml
gaussianSplat:
  uri: cache://voxel51-gaussian-splatting/train_iteration_7000/iteration-7000/converted/point_cloud.ply
```

兼容阶段 MAY 暂时接受 `.ply` 的 `mesh.uri`，但保存后的 canonical 表面 SHALL 使用 `gaussianSplat.uri`。如果实现期决定直接 hard cut，不支持 `mesh.uri: *.ply`，需要在测试和错误信息中明确说明。

迁移规则：

| 输入表面 | Runtime 解释 | 保存表面 |
|---|---|---|
| `gaussianSplat.uri: *.ply` | 3DGS node | `gaussianSplat.uri` |
| `mesh.uri: *.ply` | 兼容读取为 3DGS node | `gaussianSplat.uri` |
| `mesh.uri: *.obj/.gltf/.glb` | 普通 mesh node | `mesh.uri` |

### R2: Runtime 组件

新增运行时组件或资源挂载点，保存：

- `GaussianSplatCloud` CPU resource
- GPU resource handle（由后续 REQ 填充）
- local transform
- local-space bounds

该对象 SHALL 不伪装成 `Mesh`，也不要求 material shader reflection。

### R3: Asset 解析入口

SceneRuntime SHALL 根据 `.ply` 或 `gaussianSplat.uri` 调用 `GaussianSplatPlyLoader`。加载失败时，错误信息 SHALL 包含 scene node path 和 asset URI。

assets-downloader 的 point-cloud manifest 已经写出 `sceneUsage.gaussianSplatUri`，本 REQ SHALL 直接消费该 URI 形态，不要求 in-git 大型 PLY。

### R4: Editor 可见性

Editor tree / inspector SHALL 能识别该节点类型，至少显示 URI、splat count、bounds 和 SH degree。

### R5: 测试覆盖

测试 SHALL 覆盖：

- 使用小型 fixture scene 加载后生成一个 3DGS runtime 节点。
- 如本地存在 Voxel51 cache PLY，`assets/scenes/3dgs_train_sample.scene.yaml` 或等价 smoke scene 的 splat count 为 `741883`。
- 普通 OBJ / glTF scene 仍按原路径加载。
- 保存再加载后 `.ply` 节点使用 canonical `gaussianSplat.uri` 表面。

## 修改范围

- `src/core/scene/`
- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/core/editor/` inspector / tree 表面
- `src/test/`

## 依赖

- `REQ-076-c`
- `REQ-077-a`
- `notes/roadmaps/research/3dgs-ply-rendering/03-LX当前状态对照.md`

## 实施状态

2026-06-14 重排后状态：保留 active，未实施。

当前没有 `GaussianSplatComponent` / 等价 runtime 对象，scene/runtime/editor 也没有 3DGS 节点表面。后续 scene fixture 应优先使用小型 fixture；大型 train sample 只通过 assets-downloader cache URI 引用。
