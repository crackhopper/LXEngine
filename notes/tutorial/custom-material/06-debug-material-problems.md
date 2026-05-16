# 06 材质排错

材质问题通常不是一个点坏了，而是 YAML、shader、反射、参数写入、场景覆盖之间某个环节没对上。我们像查账一样逐层核对。

## 快速诊断表

| 现象 | 优先检查 |
|---|---|
| shader 编译失败 | GLSL 语法、include、`glslc` 输出 |
| material 加载失败 | `.material` 的 shader 名、pass 名、参数名 |
| 参数改了没效果 | shader 是否真的读取该参数 |
| 物体全黑 | light 输入、法线、颜色范围 |
| 保存后参数丢失 | `nodeMaterialOverrides` 是否写入 scene YAML |
| 重新加载失败 | YAML 类型是否和 loader 期望一致 |

## 当前仓库里的工具

| 工具 | 用途 |
|---|---|
| `ninja test_shader_compiler` | 快速验证 shader 编译和反射 |
| `LX_RENDER_DEBUG=1` | 打印渲染路径调试信息 |
| `test_generic_material_loader` | 验证通用 material loader |
| `test_material_instance` | 验证参数写入和 system-owned binding |

## 一步一步排查

先确认 shader 文件存在：

```bash
ls assets/shaders/glsl/gooch_demo.vert assets/shaders/glsl/gooch_demo.frag
```

再确认 material 指向它：

```bash
rg -n "shader: gooch_demo" assets/materials/gooch_demo.material
```

然后跑编译验证：

```bash
cd build
ninja test_shader_compiler
./src/test/test_shader_compiler
```

最后再进 editor 验证可视结果。

## 我们已经学会了什么

材质排错要按链路查：shader 能编、反射能读、YAML 能写、editor 能覆盖、scene 能保存。

## 下一步

继续 [自定义灯光](../custom-light/index.md)。
