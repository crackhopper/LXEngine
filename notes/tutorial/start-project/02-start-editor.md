# 02 启动 editor：打开主工作台

现在我们打开工作台。`lxe_editor` 是当前教程的主入口，它比底层渲染测试更适合学习场景、材质、光源和命令。

## 为什么 editor 是后续教程的共同入口

`lxe_editor` 不是单纯的 demo 窗口，它由三部分组成：

| 部分 | 类比 | 作用 |
|---|---|---|
| 画面视口 | 工作台表面 | 看见场景和选中对象 |
| ImGui 面板 | 工具架 | Scene Tree、Inspector、Console、Toolbar |
| CommandBus | 操作记录本 | 所有关键编辑动作最终都走命令 |

材质教程需要把 `.material` 挂到节点上，灯光教程需要在场景中创建 light，扩展教程需要观察 command 和 toolbar。因此我们先确认 editor 这张工作台能打开。

## 启动前需要已有的构建结果

在 `build/` 目录里：

```bash
ninja lxe_editor
./src/demos/lxe_editor/lxe_editor
```

| 命令 | 作用 |
|---|---|
| `ninja lxe_editor` | 编译 editor 可执行程序，并确保 `CompileShaders` 已经完成 |
| `./src/demos/lxe_editor/lxe_editor` | 从 build 目录运行 editor，让 runtime 能看到同步后的 `assets/` |

启动后，我们应该看到一个 editor 窗口。左侧或浮动面板会提供 Scene Tree、Inspector、Console、Toolbar 等入口。具体布局会受 `data/lxe_editor/editor_config.yaml` 影响。

## 面板、toolbar 和 CommandBus 如何接在一起

这一章只需要先记住一句话：UI 是可视化入口，关键操作最终会走 CommandBus。更完整的设计拆解放在 [Editor System 设计文档](../../design/editor-system/index.md) 中；那里会按主循环、对象归属、CommandBus、面板/toolbar、SceneRuntime、API/MCP 的顺序解释当前代码。

## 启动时打开渲染日志

如果想带渲染调试日志：

```bash
LX_RENDER_DEBUG=1 ./src/demos/lxe_editor/lxe_editor
```

## 启动失败时回到哪一步

| 现象 | 优先检查 |
|---|---|
| 没窗口 | 当前 shell 是否在图形环境下，Vulkan 驱动是否可用 |
| 启动后找不到资产 | repo root / build root 下是否能看到 `assets/` |
| toolbar 不见了 | editor 会强制恢复 toolbar 可见；若仍异常，删除 `data/lxe_editor/editor_config.yaml` 后重启 |
| shader 编译失败 | 回到 [01 环境与构建](01-environment-and-build.md) 先跑 `test_shader_compiler` |

## 我们已经学会了什么

我们已经知道 `lxe_editor` 是主学习入口，也知道它不是绕过命令系统的 GUI，而是命令系统的一层可视化外壳。

## 下一步

进入 [03 加载与保存场景](03-load-and-save-scene.md)。
