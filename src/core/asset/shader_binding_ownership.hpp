#pragma once

#include "core/asset/shader.hpp"

#include <optional>
#include <string_view>

namespace LX_core {

/*
@source_analysis.section System-owned binding：按名字切开反射结果的所有权边界
这段代码看起来只是一个常量数组加一个 `contains`，但它其实是反射层和材质层
之间的所有权契约，来源是 REQ-031。

`IShader::getReflectionBindings()` 会把 shader 里所有 descriptor binding 都暴露出来，
不区分谁应该拥有它们。材质系统如果照单全收，就会出现两类 binding 被混在一起：

- 引擎全局数据：相机、光照、骨骼等，这些数据在渲染循环里由引擎统一喂入，
  材质根本不应该有写入权限，也不该为它们分配 buffer
- 材质参数：baseColor、贴图、采样器这些，才是材质 instance 真正管理的资产

`kSystemOwnedBindings` 和 `isSystemOwnedBinding()` 就是这条分隔线的物理实现。
`MaterialTemplate::rebuildMaterialInterface()` 在构造 canonical material binding
表时，会先用这个函数把反射结果里的系统名字过滤掉，剩下的才是 material-owned 的部分。

名字集合故意保持很小、很固定。扩展它意味着“又一个 binding 被声明为引擎全局”，
这是带 spec 变更语义的决定，而不是实现细节。
*/

inline constexpr std::string_view kSystemOwnedBindings[] = {
    "CameraUBO",
    "LightUBO",
    "Bones",
};

inline bool isSystemOwnedBinding(std::string_view name) {
  for (auto sv : kSystemOwnedBindings)
    if (sv == name)
      return true;
  return false;
}

/*
@source_analysis.section getExpectedTypeForSystemBinding：把保留名字钉在固定的 descriptor 类型上
只是把名字列入 `kSystemOwnedBindings` 还不够；引擎必须进一步声明：
这些保留名字对应的 descriptor 类型本身也不能随便换。

如果某一份 shader 意外把 `CameraUBO` 声明成 `StorageBuffer` 或 `Texture2D`，
那就算名字对得上，它表达的语义也已经偏离引擎约定。`getExpectedTypeForSystemBinding()`
用来在这种情况下给上层一个可比对的参照类型，让反射校验能尽早把偏离挡掉，
而不是等到 descriptor set 写入阶段才炸。

非保留名字返回 `std::nullopt`，表示“这一条不归这里管”，上层可以判断是否进入
material-owned 路径而不是误用这个函数做通用类型查询。
*/

inline std::optional<ShaderPropertyType>
getExpectedTypeForSystemBinding(std::string_view name) {
  if (name == "CameraUBO")
    return ShaderPropertyType::UniformBuffer;
  if (name == "LightUBO")
    return ShaderPropertyType::UniformBuffer;
  if (name == "Bones")
    return ShaderPropertyType::UniformBuffer;
  return std::nullopt;
}

} // namespace LX_core
