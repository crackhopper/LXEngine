# 05 启动排错：先定位失败发生在哪一层

排错像检查工作室的电路图。我们不靠猜，而是把失败分成构建、shader、窗口、资产、editor 状态几类，一类一类排除。

## 第一问：失败发生在哪一步

| 失败点 | 先跑什么 | 判断 |
|---|---|---|
| CMake 配置失败 | `cmake .. -G Ninja` | 依赖没找到 |
| shader 编译失败 | `ninja test_shader_compiler` | shaderc / glslc / GLSL 问题 |
| Vulkan / offline backend 失败 | `ninja test_vulkan_offline_renderer` | driver / Vulkan device / offline backend 问题 |
| editor 失败 | `ninja lxe_editor` 后运行 | editor session / project / UI 状态问题 |
| project / scene 保存异常 | `project status`、`scene list`、`scene save` | project 是否打开、active scene 是否绑定、document capture |

如果 `cmake` 还没有成功，就不要继续查 editor。若 `test_shader_compiler` 已失败，就先解决 shader 工具链。排错顺序越靠前，越能避免把多个问题混在一起。

## 构建与 shader 问题

| 现象 | 先看哪里 |
|---|---|
| `shaderc not found` | `src/infra/CMakeLists.txt` 的 shaderc 查找逻辑，以及 `SHADERC_DIR` |
| `glslc: command not found` | Vulkan SDK / shader tools 是否安装 |
| shader 反射失败 | `.spv` 是否生成，GLSL binding 是否和材质 YAML 对齐 |

## 窗口与 Vulkan 问题

| 变量 | 作用 |
|---|---|
| `LX_RENDER_DEBUG=1` | 打印渲染调试信息 |
| `LX_RENDER_DEBUG_CLEAR=1` | 改清屏色，确认 render pass 是否执行 |
| `LX_RENDER_DISABLE_CULL=1` | 临时关闭背面剔除，排查 winding |
| `LX_RENDER_DISABLE_DEPTH=1` | 临时关闭深度测试 |

这些环境变量只帮助我们观察渲染过程。驱动、显示环境或 Vulkan 设备不可用时，仍然要先回到系统层检查。

## project 与 scene 保存问题

project 是保存边界。`scene save` 只保存当前 project 的 active scene；如果没有打开 project，先执行 `project init empty my_first_project` 或 `project open <id-or-path>`。如果 scene 看起来没有切换，先用 `project status` 确认 `activeScene`，再用 `state summary` 确认 runtime sceneName 已经变化。

| 现象 | 先检查 |
|---|---|
| `scene save` 提示没有 project | 先创建或打开 project |
| `scene open <id>` 失败 | `scene list` 里是否有这个 scene id |
| 新建 scene 后保存状态异常 | 先 `scene save`，再 `project save` |
| 场景找不到模型或材质 | project 资产目录和 runtime `assets/` 是否存在 |

## editor 本地状态问题

| 文件 | 作用 |
|---|---|
| `.tmp/notes-serve.log` | notes 站点日志 |
| `data/lxe_editor/runtime_state.yaml` | editor HTTP / WebSocket / MCP discovery |
| `data/lxe_editor/editor_config.yaml` | editor UI 配置 |
| `data/lxe_editor/editor_data.yaml` | editor 本地数据，例如 command history |

### 布局异常时重置本地配置

先关闭 editor，再临时移走 UI 配置：

```bash
mv data/lxe_editor/editor_config.yaml data/lxe_editor/editor_config.yaml.bak
```

重启后 editor 会生成新配置。

## 我们已经学会了什么

我们已经把启动问题拆成了可验证的小块：工具链、shader、窗口、project/scene 保存边界和 editor 状态。

## 下一步

继续 [自定义材质](../custom-material/index.md)。
