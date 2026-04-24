# 01 · 类型擦除接口 + 类型标签校验

> 代码位置：`src/core/asset/parameter_buffer.cpp`（`ParameterBuffer::writeBindingMember`），
> 调用方：`src/core/asset/material_instance.cpp`（一组 `setParameter` 重载）。

## 1. 现场

材质实例需要把 CPU 侧的材质参数（`float` / `int` / `Vec3f` / `Vec4f` / …）写进一段和 shader 中某个 UBO 布局严格对应的内存。
真实的写入入口长这样：

```cpp
// src/core/asset/parameter_buffer.cpp
void ParameterBuffer::writeBindingMember(StringID memberName,
                                         const void *src,
                                         usize nbytes,
                                         ShaderPropertyType expected) {
  for (const auto &member : m_binding.get().members) {
    if (StringID(member.name) != memberName) {
      continue;
    }
    assert(member.type == expected &&
           "MaterialInstance setter type does not match reflected member type");
    assert(static_cast<usize>(member.offset) + nbytes <= m_buffer.size() &&
           "UBO write would overflow the reflected buffer");
    std::memcpy(m_buffer.data() + member.offset, src, nbytes);
    m_dirty = true;
    return;
  }
  assert(false &&
         "MaterialInstance setter: member not found in parameter buffer");
}
```

外层的 `MaterialInstance::setParameter(..., float)` / `(..., i32)` / `(..., const Vec3f &)` /
`(..., const Vec4f &)` 只是把“具体类型”压扁成三个平凡参数再转发进来：

```cpp
// src/core/asset/material_instance.cpp
void MaterialInstance::setParameter(StringID bindingName, StringID memberName,
                                    const Vec3f &value) {
  auto *parameterBuffer = findParameterBuffer(bindingName);
  assert(parameterBuffer && "...");
  if (parameterBuffer)
    parameterBuffer->writeBindingMember(memberName, &value, sizeof(float) * 3,
                                        ShaderPropertyType::Vec3);
}
```

## 2. 它在解决什么问题

`ParameterBuffer` 背后是一段 `std::vector<u8>`，但它不是随便一段内存——它必须精确匹配 shader 里某个 UBO 的布局（从 SPIRV-Cross 反射出来的 `members` 列表里每项都有 `name / type / offset`）。

写入方（MaterialInstance）写错任何一条都会静默破坏 GPU 侧的渲染结果：

- 写错 **成员名**：写到了不存在的 offset，或者写到了别的成员身上。
- 写错 **类型**：`float` 写到 `vec4` 的位置，shader 读到的数据完全乱码。
- 写错 **大小**：越界覆盖了别的字段或堆内存。

所以真正需要保证的是三件事，一件都不能省：

1. 名字存在。
2. 类型和反射结果一致。
3. 写入长度不越界。

## 3. 手法本身

这段代码用的就是题目里说的那种写法，我愿意把它概括成三个术语，它们从不同角度描述同一件事：

### 3.1 Tagged type-erased interface

接口签名是类型擦除的（`const void *src, usize nbytes`），但额外带一个 **类型 tag**（`ShaderPropertyType expected`）。
被调方不再“相信”调用方给了正确类型，而是把这份信任显式地搬成了一次运行时断言：

```cpp
assert(member.type == expected && "... type does not match ...");
```

这在 C 里有个经典名字叫 **tagged union / discriminated write**：一块无类型内存 + 一个 tag 判别式。
C++ 里更正式的说法是 **runtime-checked type erasure**：对外丢掉静态类型，对内用一个运行时标签重建了类型不变式。

### 3.2 Narrow waist（沙漏腰）

从架构形状上看这是一个典型的“沙漏”：

```
float/i32/Vec3f/Vec4f/…  （上口：重载很多的强类型入口）
         │
         ▼
writeBindingMember(void*, nbytes, ShaderPropertyType)   ← 腰（唯一窄口）
         │
         ▼
std::memcpy 到 UBO 内存
```

- **上口宽**：任意具体 C++ 类型都可以进来（通过各自的 `setParameter` 重载）。
- **腰窄**：腰部只看三样东西——指针、大小、类型 tag，不关心上口是什么类型。
- **下口窄**：最终就是一次 `memcpy`。

这就是 Google/Rust/Git 等项目里经常被引用的 **narrow waist / hourglass** 设计：
把所有新增的“具体类型入口”挡在上层重载里，腰部本身不需要随新类型增长。

加新参数类型（比如 `Vec2f` / `Mat4f` / 颜色）只要在上层多一个重载，腰部一行代码都不用改。

### 3.3 Reflection-driven contract + runtime guard

更正式一点的名字是：**contract enforced by reflection, validated at the write site**。

- 契约的真身在 shader + `ShaderResourceBinding::members`（offset / type / size 是反射出来的）。
- CPU 侧不重新维护一份 offset 表，不 codegen 一堆 struct；
- 写入时只做两件事：*查名字拿元数据* → *对 tag / 长度做 assert*。

这和一些引擎里把 UBO 布局 **再在 C++ 里写一遍 struct** 的做法本质是相反的选择：
这里不复制契约，只在使用点兜底校验。

## 4. 和“用模板/强类型接口”做对比

如果不用这个手法，大家通常会写这样两种替代版本：

### 替代 A：纯模板

```cpp
template <class T>
void write(StringID memberName, const T &value);
```

好处是编译期就知道类型，坏处在这个场景里其实很多：

- 模板必须在 header 里暴露实现，或者走 explicit instantiation；
- 需要一张 `T → ShaderPropertyType` 的 traits 映射表，否则照样要在运行时对反射 tag 做比较；
- shader 的布局信息 **不在类型系统里**（是反射出来的运行时数据），所以编译期强类型其实挡不住“GLSL 里写的是 vec3，C++ 传了 Vec4f” 这类错——最后还是得做一次运行时比较。

也就是说：**类型安全想挡住的那类错，主要来自 CPU 类型 vs shader 声明，而 shader 声明在编译期根本还不存在。** 模板挡不住它。

### 替代 B：每种类型一个具名函数

```cpp
void writeFloat(...);
void writeInt(...);
void writeVec3(...);
void writeVec4(...);
```

每多一种类型就要新开一个函数 + 重复 `find member → assert offset → memcpy` 逻辑，腰部会被撑开，很容易演化成一堆几乎一样的函数 + 漂移。

### 当前方案

当前写法是把这两种替代的优点拿走、把它们的代价避开：

- 上层仍然有一组 **类型明确的重载** `setParameter(..., float) / (..., i32) / ...`，
  使用者的 API 看起来依然是强类型的；
- 腰部 `writeBindingMember` 只处理 *(void\*, nbytes, tag)*，反射结果直接就是类型不变式的 single source of truth；
- 类型一致性用 `assert(member.type == expected)` 显式证明，既写在代码里又读得到——不是“我相信它对”而是“我在这里检查它对”。

所以题目里那个直觉——“接口看起来弱类型，但因为带了 tag 实际上仍然是强类型”——说的其实就是：
**静态类型 + 反射 tag 两张网，总有一张挡住错误；任何一个维度错了（名字、类型、长度）都会在 debug build 里第一时间触发 assert。**

## 5. 适用场景 / 退化路径

这个手法很适合：

- 写入目标的 **契约来自外部**（shader、schema、protobuf 描述符、配置表）；
- 你 **不想**把外部契约重新在 C++ 类型系统里复刻一遍；
- 每次调用都能在运行时承担一次廉价的 assert（通常是几次整数比较）；
- 你希望腰部函数非常小、变动频率非常低。

不合适的信号：

- **极高频热路径**：每写一个 int 都做一次 `members` 线性查找和 assert，可能会变成开销。
  解法是缓存 `member*` / `offset` —— 但那时这个手法其实也还在，只是把“查找”挪到了外面。
- **完全静态的布局**：如果 UBO / struct 是写死的、不会随 shader 变，那 codegen 一个强类型 struct 会更诚实。
- **Release 里 assert 被裁掉 + 还要防御调用方作恶**：这时 `void*` 接口就是真的不安全，要么换显式 tagged union，要么换 `std::variant`。

## 6. 一句话总结

> 在外部契约来自运行时（shader、schema、配置）的场景里，
> 把入口收成 **`(const void*, size, type_tag)` 的 narrow waist**，
> 让反射结果充当 single source of truth，
> 再用 `assert(tag 一致)` 把“类型安全”从编译期挪到写入点——
> 这是比模板更朴素、但在这个问题上更贴题的选择。
