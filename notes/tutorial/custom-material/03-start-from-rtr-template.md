# 03 从 RTR 模板开始

我们不从空白文件开始，而是从已有模板复制。模板像练习册里的例题：结构已经正确，我们只改公式。

## 当前模板

| 文件 | 作用 |
|---|---|
| `assets/materials/rtr_experiment_template.material` | 材质 YAML |
| `assets/shaders/glsl/rtr_experiment_template.vert` | 顶点 shader |
| `assets/shaders/glsl/rtr_experiment_template.frag` | 片元 shader |

## 从复制模板开始降低变量

复制一份新材质：

```bash
cp assets/materials/rtr_experiment_template.material assets/materials/gooch_demo.material
cp assets/shaders/glsl/rtr_experiment_template.vert assets/shaders/glsl/gooch_demo.vert
cp assets/shaders/glsl/rtr_experiment_template.frag assets/shaders/glsl/gooch_demo.frag
```

把 `gooch_demo.material` 里的 shader 名改成：

```yaml
shader: gooch_demo
```

然后重新构建 shader 验证：

```bash
cd build
ninja test_shader_compiler
./src/test/test_shader_compiler
```

如果新增 shader 文件后 CMake 没看到它，重新 configure：

```bash
cmake .. -G Ninja
```

## 为什么先复制模板

模板已经帮我们处理了几件容易出错的事：

| 问题 | 模板提供的答案 |
|---|---|
| shader 文件命名 | `<shader>.vert` / `<shader>.frag` |
| pass 名 | `Forward` |
| 参数从哪里来 | `.material` 的 `parameters` |
| 光源从哪里来 | `SceneLightsUBO` |

## 我们已经学会了什么

新增实验材质时，最稳的路径是复制一个能工作的模板，然后一次只改一件事。

## 下一步

进入 [04 Gooch Shader](04-write-gooch-shader.md)。
