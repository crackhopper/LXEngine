# REQ-045-b: Procedural Runtime Parameter Stream

> 2026-05-19 新增：本 REQ 把 `iTime`、`iResolution`、音频模拟值这类每帧变化的数据从手工节点覆盖中分离出来，形成可重复使用的 runtime 参数流。

## 背景

`REQ-045-a` 可以让 procedural shader 作为普通材质出现，但 Shadertoy 风格效果需要每帧变化的系统输入。若只依赖 `nodeMaterialOverrides`，每帧写入会污染 scene document，也会把“当前帧状态”和“作者保存的默认参数”混在一起。

我们需要一个明确的 opt-in 语义：节点声明自己需要 procedural runtime 参数，runtime 每帧把 clock/window 信息写入对应材质 instance，不改变材质模板和 pipeline identity。

## 目标

1. 支持 `iTime` / `iResolution` 等 Shadertoy 兼容参数。
2. 每帧更新只影响 runtime material instance，不写回 scene document。
3. 参数更新不触发 scene rebuild，不改变 pipeline identity。
4. 让多个 procedural 节点可以共享同一套更新逻辑。

## 需求

### R1: Scene document 表达 procedural 参数流 opt-in

新增节点级字段：

```yaml
proceduralMaterial:
  enabled: true                         # -> SceneNodeDocument::proceduralMaterial
  binding: ShadertoyUBO                 # -> MaterialInstance binding name
  timeMember: time                      # -> float
  resolutionMember: resolution          # -> vec4
  audioBandsMember: audioBands          # -> vec4, optional
```

要求：

- `enabled` 默认为 `false`。
- `binding` 默认为 `ShadertoyUBO`。
- member 名默认使用上表值。
- 保存 scene 时只保存显式开启的节点。

### R2: Runtime 每帧写入参数

`SceneRuntime` 提供一个 per-frame 更新入口，接收 clock 和 viewport/window size。

要求：

- `timeMember` 写入 `Clock::totalTime()`。
- `resolutionMember` 写入 `{width, height, 1/width, 1/height}`。
- `audioBandsMember` v1 写入模拟或 author-provided 值，真实音频留给 `REQ-045-c`。
- 写入后调用 `MaterialInstance::syncGpuData()`。
- 如果 shader 缺少某个 optional member，跳过并保持稳定。
- 如果 required member 类型不匹配，返回可诊断错误或记录一次警告。

### R3: Editor 主循环调用 runtime 参数流

`lxe_editor` update hook 在 camera/viewport 更新后调用 runtime 参数流。

要求：

- 使用 scene view 或 window size 作为 `resolution`。
- 不触发 `EngineLoop::requestSceneRebuild()`。
- 不把当前帧值写进 `nodeMaterialOverrides`。

### R4: Inspector/CommandBus 不把 runtime 参数当作作者覆盖

Runtime 参数可以被 Inspector 显示为当前值，但不应默认保存为 node override。

要求：

- `nodeMaterialOverrides` 继续表达作者保存的静态默认值。
- runtime 参数流覆盖发生在每帧末端，优先级高于默认值。
- 若用户手动覆盖同名字段，下一帧 runtime 参数流会重新写入当前帧值。

### R5: 测试覆盖

至少覆盖：

- scene document round-trip 保存 `proceduralMaterial` opt-in。
- per-frame 更新能写入 `time` 和 `resolution`。
- per-frame 更新不新增或修改 `nodeMaterialOverrides`。
- 缺少 optional `audioBands` 时不失败。
- 类型不匹配时返回稳定诊断。

## 修改范围

- `src/demos/lxe_editor/scene_document.*`
- `src/demos/lxe_editor/scene_runtime.*`
- `src/demos/lxe_editor/main.cpp`
- `src/test/integration/test_scene_document.cpp`
- `src/test/integration/test_scene_runtime.cpp`

## 边界与约束

- 本 REQ 不引入全局 uniform buffer。
- 本 REQ 不新增 backend descriptor set。
- 本 REQ 不做真实音频采集。
- 本 REQ 不支持 Shadertoy multi-buffer feedback。

## 依赖

- `REQ-045-a`
- `openspec/specs/engine-loop/spec.md`
- `openspec/specs/material-system/spec.md`

## 后续工作

- `REQ-045-c` 引入真实 audio texture 和 FrameGraph/post-process 路径。

## 实施状态

2026-06-14 复核关闭：procedural runtime 参数流已完成并仍由当前 scene/runtime 代码支持。后续材质系统主线不再把本 REQ 作为 active 实施单元。

已完成。

完成内容：

- scene document opt-in、`SceneRuntime::updateProceduralMaterials(...)`、`lxe_editor` 主循环调用、`time` / `resolution` / fake `audioBands` 写入。
- Runtime 参数更新不写回 `nodeMaterialOverrides`；required runtime 参数类型不匹配会返回稳定诊断。
- `SceneRuntime::nodeMaterialParametersForNode(...)` 会把 `proceduralMaterial` 管理的 `time` / `resolution` / `audioBands` 标记为 runtime-owned，Inspector 以 disabled 控件展示这些当前值，不把它们当作作者覆盖入口。
- CommandBus 支持 `get/set <path>.proceduralMaterial.enabled`，可显式开关 procedural runtime opt-in，并通过 undo 恢复上一次状态。

验证命令：

- `cmake --build build --target test_command_bus test_inspector_panel test_scene_runtime lxe_editor -j2`
- `./build/src/test/test_command_bus`
- `./build/src/test/test_inspector_panel`
- `./build/src/test/test_scene_runtime`
