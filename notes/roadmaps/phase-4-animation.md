# Phase 4 · 动画

> **目标**：让已有的 `Skeleton` 资源能播放真正的动画。
>
> **依赖**：Phase 2（需要 `Clock` 推进播放时间）、Phase 3（AnimationClip 是 asset）。
>
> **可交付**：`demo_animated_character` — 一个加载自 GLTF 的角色，播放 idle / walk / run 动画，按键触发 jump 并混合过渡。

## 范围与边界

**做**：
- `AnimationClip` 资源（每骨骼的位置 / 旋转 / 缩放关键帧）
- `AnimationPlayer` 组件（播放单个 clip，时间推进）
- `SkeletonPose` 中间结构（按当前时间插值出当前 pose）
- 跨 clip 混合（crossfade）
- 有限状态机 + 层级（layer）
- GLTF 动画导入
- Root motion 提取
- 动画事件

**不做**：
- 反向运动学（IK）— 专题系统，独立迭代
- Morph target / blendshape — 先绑到 `if you need` 时再补
- Motion matching — 超出小型引擎范畴

---

## 前置条件

- Phase 2：`SceneNode` + `Transform` + `Clock`
- Phase 3：`AssetHandle<AnimationClip>` 能被 scene 引用
- 现状：`Skeleton` 资源 (`src/core/asset/skeleton.hpp`) + `SkeletonUBO` 已就位

---

## 工作分解

### REQ-401 · AnimationClip 资源

```cpp
struct Keyframe {
    float time;    // 秒
    Vec3f value;   // 或 Quatf for rotation
};

struct BoneTrack {
    StringID boneName;
    std::vector<Keyframe> positionKeys;
    std::vector<Keyframe> rotationKeys;  // Quatf
    std::vector<Keyframe> scaleKeys;
};

class AnimationClip {
public:
    StringID name;
    float    duration;  // 秒
    float    fps;
    std::vector<BoneTrack> tracks;
    bool     looping = true;
};
```

- 是一个 `IGpuResource`？**不是**，它不直接上 GPU。只是 CPU 侧 asset。
- 可序列化、可按 GUID 引用

**验收**：`test_animation_clip.cpp`：构造一个 2 秒 clip，在 t=0.5 / 1.0 / 1.5 秒查询 pose。

### REQ-402 · SkeletonPose 插值

```cpp
struct SkeletonPose {
    std::vector<Vec3f>  positions;  // per bone
    std::vector<Quatf>  rotations;
    std::vector<Vec3f>  scales;
};

class PoseSampler {
public:
    static void sample(const AnimationClip& clip, float time, SkeletonPose& out);
};
```

- 线性插值（LERP）for 位置 / 缩放
- 球面线性插值（SLERP）for 旋转
- 超出 duration 的行为由 `clip.looping` 决定

**验收**：`test_pose_sampler.cpp`：给定单 bone clip，在插值点的 pose 符合预期。

### REQ-403 · Pose → Skeleton UBO

把采样出的 `SkeletonPose` 转换成 `SkeletonUBO` 的 4×4 矩阵数组：

```cpp
void SkeletonResource::applyPose(const SkeletonPose& pose);
```

- 每根骨骼：`localMat = TRS(pose.pos, pose.rot, pose.scale)`
- 按骨骼层级乘出 `worldMat`
- `finalMat = worldMat * inverseBindMat`
- 写入 `SkeletonUBO.bones[i]`，标 dirty

**验收**：GLTF 里的绑定姿势 + 一个动画 clip → GPU 端骨骼矩阵数组与 Blender 验证一致。

### REQ-404 · AnimationPlayer 组件

```cpp
class AnimationPlayer {
public:
    void play(AnimationClipHandle clip, bool restart = false);
    void pause();
    void resume();
    void stop();

    void update(float dt);   // 被 Engine::onUpdate 调用
    void applyToSkeleton(Skeleton& skel);

    float time() const;
    float normalizedTime() const;
    bool  isFinished() const;

private:
    AnimationClipHandle m_current;
    float m_time = 0;
    bool  m_playing = false;
    float m_playbackSpeed = 1.0f;
};
```

- 挂在 `SceneNode` 上（和 `IRenderable` 一样可选）
- `Engine::onUpdate` 遍历 scene tree，调用每个 player 的 `update(dt)`

**验收**：`demo_animated_character` v1：一个角色在 idle 动画下呼吸。

### REQ-405 · Crossfade 过渡

```cpp
class AnimationPlayer {
    void crossfadeTo(AnimationClipHandle next, float duration);
    // 内部：持有 from / to 两个 clip 的播放头，线性混合 pose
};
```

- `SkeletonPose` 支持 lerp：`Pose::lerp(a, b, t)` 按 bone 做 pos lerp + quat slerp + scale lerp

**验收**：idle ↔ walk 之间过渡无骨骼弹跳。

### REQ-406 · 状态机

```cpp
class AnimationStateMachine {
public:
    StringID currentState() const;
    void     setState(StringID state);  // 直接切换（通常不用）
    void     trigger(StringID transition);

    void addState(StringID name, AnimationClipHandle clip, bool looping = true);
    void addTransition(StringID from, StringID to, float duration);
    void addAnyStateTransition(StringID to, float duration);  // 任意状态可进

    void update(float dt, AnimationPlayer& player);
};
```

- 配置驱动：可以从 JSON / YAML 构造
- 一个 `SceneNode` 挂 `AnimationPlayer` + `AnimationStateMachine`

**验收**：按 W → 触发 `walk` transition → 过渡 0.2 秒。

### REQ-407 · 多层（Layer）

- 基础层：全身动画（idle / walk / run）
- Upper body 层：上半身覆盖（aim / reload）
- 每个层有 mask（哪些骨骼受此层影响）+ weight

实现：每个 layer 输出一个 pose，按 mask + weight 加权混合到最终 pose。

**验收**：角色走路的同时上半身播放挥手。

### REQ-408 · 动画导入

- 资产导入器扩展：解析外部动画文件 → `AnimationClip`
- 节点名字 → StringID 映射（`BoneTrack.boneName`）
- 支持 linear / step / cubic-spline 插值模式（cubic 可先降级成 linear）

**选型参考**：优先 glTF 这类跨工具链标准格式；其他格式通过中间转换接入。

**验收**：能加载主流工具链输出的动画文件。

### REQ-409 · Root Motion

- 某些动画的根骨骼带有位移 / 旋转（走路前进）
- `AnimationClip` 添加 `extractRootMotion` 开关
- 提取后的 root delta 返回给 gameplay 代码，由它决定怎么应用到 Transform（走、滑步、被锁定）

**验收**：角色播放 walk 循环时位置实际移动。

### REQ-410 · 动画事件

- `AnimationClip` 可以挂事件：`{time: 0.3, eventName: "Footstep"}`
- `AnimationPlayer::update` 在越过事件时间点时发事件到系统（依赖 Phase 6 的事件总线；没有时先直接回调）

**验收**：walk 的脚步声触发时机正确。

---

## 里程碑

### M4.1 · 单 clip 播放

- REQ-401 + REQ-402 + REQ-403 + REQ-404 完成
- demo：一个角色播放一个 idle 循环

### M4.2 · 过渡 + 状态机

- REQ-405 + REQ-406 完成
- demo：idle → walk → run 切换流畅

### M4.3 · 完整动画管线

- REQ-407 + REQ-408 + REQ-409 + REQ-410 完成
- demo：`demo_animated_character`，走路 + 挥手 + 脚步音响触发

---

## 风险 / 未知

- **Quaternion 稳定性**：SLERP 的 dot < 0 情况要取反。累积浮点漂移需要周期性 normalize。
- **Mixamo / glTF 骨骼命名差异**：名字约定跨工具链不稳定。给 loader 一个 "rename rules" 入口。
- **性能**：一个角色 ~60 骨骼，状态机 + 多 layer 在主线程跑，100 个角色左右会吃满 1 个 core。解决：Phase 6 之后考虑 job 化。
- **动画烘焙 vs 运行时采样**：大量关键帧的 clip（手工动画）vs 烘焙过的 30Hz 采样。先只支持第二种，降低复杂度。

---

## 与现有架构的契合

- `SkeletonUBO` 已经是 GPU dirty-sync 路径上的 resource，本阶段只需要提供"每帧写入新的骨骼矩阵"的 CPU 侧 driver。
- `IGpuResource` 的 dirty 通道天然适配"每帧动画采样后标 dirty"。
- `StringID` 给骨骼名、动画名、transition 名提供统一 intern。
- `AssetHandle<AnimationClip>` 走 Phase 3 的资产系统，支持热重载（策划调完动画不重启即刻生效）。

---

## 与 AI-Native 原则的契合

| 原则 | 本阶段如何落实 |
|------|--------------|
| [P-1 确定性](principles.md#p-1-确定性是架构级不变量) | 动画采样是纯函数：(clip, time) → pose，确定性天然成立 |
| [P-10 Provenance](principles.md#p-10-资产血统--provenance) | AnimationClip 的 provenance 支持"这段动画是 AI 生成的 + prompt"，为 Phase 11 动画生成打基础 |
| [P-20 渲染/模拟可分离](principles.md#p-20-渲染与模拟的可分离) | 动画采样属于模拟层，headless 模式下仍然推进 |

---

## 下一步

Phase 4 完成后进入 [Phase 5 物理](phase-5-physics.md)。角色有动画了，下一步让它能和世界互动。
