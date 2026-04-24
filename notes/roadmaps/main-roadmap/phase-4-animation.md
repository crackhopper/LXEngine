# Phase 4 · 动画

> **目标**：从“`Skeleton` 静态资源”推进到“骨骼动画播放 + 状态机混合”。角色能走、能跑、能切换动作。
>
> **依赖**：Phase 2（`Transform` / 层级 + `Clock`）+ Phase 3（AnimationClip 作为 asset）。
>
> **可交付**：
> - `demo_character_walk` — 角色加载 `.gltf` 动画，按 WASD 切 idle / walk / run
> - `AnimationPlayer` 组件可挂在任意骨骼对象上

## 当前实施状态（2026-04-24）

**未开工**。骨骼数据结构已落地，没有动画播放器。

| 条目 | 状态 | 备注 / 位置 |
|------|------|-------------|
| `Skeleton` / `SkeletonUBO`（GPU 可消费） | ✅ | `src/core/asset/skeleton.*` |
| `USE_SKINNING` shader variant + `inBoneIDs` / `inBoneWeights` vertex layout | ✅ | `assets/shaders/glsl/blinnphong_0.*` |
| AnimationClip 资源 | ❌ | REQ-401 |
| AnimationPlayer 组件 | ❌ | REQ-402 |
| 状态机 / 过渡混合 | ❌ | REQ-403 |
| Pose 层混合（上/下半身） | ❌ | REQ-404 |
| Root motion | ❌ | REQ-405 |

## 范围与边界

**做**：

- `AnimationClip` 资源（关键帧 + 插值）
- `AnimationPlayer` 组件（play / pause / seek / blend）
- 简单状态机（state + transition + condition）
- Pose 层混合（按 weight + bone mask）
- Root motion（root bone 位移转到 Transform）

**不做**：

- IK（目标驱动）
- 面部 / 表情
- 物理驱动动画（ragdoll → Phase 5）
- Morph target / blend shape

## 前置条件

- Phase 3 animation clip 资产加载（`.gltf` 动画通道或自定义 `.anim`）
- Phase 2 `Transform` 层级（root motion 回写位置）

## 工作分解

### REQ-401 · AnimationClip 资源

```cpp
struct AnimationChannel {
  StringID           boneName;
  std::vector<float> times;
  std::vector<KeyF>  values;   // Vec3 / Quat / Vec3 依通道语义
  Interpolation      interp;   // linear / cubic
};

class AnimationClip {
  float                         duration;
  std::vector<AnimationChannel> channels;
  AssetGuid                     guid;
};
```

- 从 `.gltf` 动画通道导入
- 每通道对应一个骨骼 + 一个 TRS 属性

**验收**：加载 `.gltf` 动画得到 `AnimationClip`；`duration` 与文件一致。

### REQ-402 · AnimationPlayer 组件

```cpp
class AnimationPlayer {
public:
  void play(AnimationClipSharedPtr clip, bool loop = true);
  void stop();
  void seek(float t);
  void update(float dt);   // 推进时间 + 采样 + 写 Skeleton bone matrices
private:
  SkeletonSharedPtr      m_skeleton;
  AnimationClipSharedPtr m_clip;
  float m_time = 0.0f;
};
```

- `update(dt)` 按通道插值得 bone pose，写入 `Skeleton` 的 bone matrices
- 通过 `SkeletonUBO` 上传 GPU

**验收**：挂到 `scene_viewer` 角色上，能看到骨骼动画循环播放。

### REQ-403 · 状态机

```cpp
class AnimStateMachine {
  struct State      { AnimationClipSharedPtr clip; };
  struct Transition {
    StringID from, to;
    std::function<bool()> condition;
    float blendDuration;
  };
  StringID currentState;
  // ...
};
```

- 条件可查 action state（Phase 2 action mapping）或自由参数
- 过渡期 blend 两 clip

**验收**：WASD 切 idle ↔ walk ↔ run，无姿态跳跃。

### REQ-404 · Pose 层混合

- 两路 pose 按 weight 加权
- 骨骼 mask（某些骨骼只受某路 pose 影响）

**验收**：角色边走边挥手（走路 + 挥手 mask 混合）。

### REQ-405 · Root motion

- `AnimationPlayer` 读 root bone 位移
- 写回宿主 `Transform::localPosition`
- 剩余骨骼相对运动保持

**验收**：角色根据动画自己往前走，无脚本驱动。

## 里程碑

| M | 条件 | demo |
|---|------|------|
| M4.1 · Clip 加载 | REQ-401 | `.gltf` 动画加载成功 |
| M4.2 · 单 clip 播放 | REQ-402 | 角色循环播放 idle |
| M4.3 · 状态机 | REQ-403 | WASD 切 idle / walk / run |
| M4.4 · Pose 混合 | REQ-404 | 上/下半身独立动画 |
| M4.5 · Root motion | REQ-405 | 动画驱动位移 |

## 风险 / 未知

- **动画压缩**：`.gltf` 动画可能极大。首版不压缩；Phase 12 打包再考虑。
- **骨骼命名约定**：不同资产命名不同。需要映射表或强制 `.gltf` 原始命名。
- **插值精度**：cubic 插值需 tangent / 控制点。首版只做 linear + slerp。
- **Blend duration**：低帧率下混合期过短导致跳变。首版固定 0.2s，后续可配置。

## 与 AI-Native 原则契合

- [P-5 语义查询](principles.md#p-5-语义查询层)：`query.animations({playing:true, on_character:"player"})` 让 agent 查“谁在播什么”。
- [P-7 多分辨率](principles.md#p-7-多分辨率观察)：动画状态 summary（“player: walk 40% → run 60%”）。
- [P-16 多模态](principles.md#p-16-文本优先--文本唯一)：动画 dump / pose 文本化给 agent。

## 与现有架构契合

- `SkeletonUBO` 已 GPU 可消费；`AnimationPlayer::update` 写 bone matrices 即接入现有材质路径。
- `USE_SKINNING` shader variant 已被 SceneNode 校验；动画播放不需要新 shader。

## 下一步

- [Phase 5 物理](phase-5-physics.md)：ragdoll 依赖动画 pose → 物理的转换。
- [Phase 6 gameplay](phase-6-gameplay-layer.md)：状态机条件由脚本驱动。
