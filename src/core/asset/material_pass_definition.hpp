#pragma once

#include "core/asset/shader.hpp"
#include "core/utils/string_table.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace LX_core {

enum class CullMode : uint8_t { None, Front, Back };
enum class CompareOp : uint8_t { Less, LessEqual, Greater, Equal, Always };
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, OneMinusSrcAlpha };

inline const char *toString(CullMode m) {
  switch (m) {
  case CullMode::None:
    return "CullNone";
  case CullMode::Front:
    return "CullFront";
  case CullMode::Back:
    return "CullBack";
  }
  return "CullUnknown";
}

inline const char *toString(CompareOp op) {
  switch (op) {
  case CompareOp::Less:
    return "Less";
  case CompareOp::LessEqual:
    return "LessEqual";
  case CompareOp::Greater:
    return "Greater";
  case CompareOp::Equal:
    return "Equal";
  case CompareOp::Always:
    return "Always";
  }
  return "CmpUnknown";
}

inline const char *toString(BlendFactor f) {
  switch (f) {
  case BlendFactor::Zero:
    return "Zero";
  case BlendFactor::One:
    return "One";
  case BlendFactor::SrcAlpha:
    return "SrcAlpha";
  case BlendFactor::OneMinusSrcAlpha:
    return "OneMinusSrcAlpha";
  }
  return "BlendUnknown";
}

/*
@source_analysis.section RenderState：pass 级固定功能状态的可签名快照
`RenderState` 只描述一个 pass 在固定功能阶段上的结构性选择：
剔除、深度测试/写入、比较函数和混合模式。它不关心材质参数值，
也不关心 shader 内部逻辑；它回答的问题是“同一份几何和 shader，
在这一步要用怎样的 raster/depth/blend 规则去提交 pipeline”。

这里同时保留 `getHash()` 和 `getRenderSignature()` 两条导出路径：

- `getHash()` 面向 C++ 侧缓存键，追求便于组合和快速比较
- `getRenderSignature()` 面向 `StringID` 组合签名，追求结构可追踪性

也就是说，这个类型不是运行时开关集合，而是 pass 身份的一部分。
*/
struct RenderState {
  CullMode cullMode = CullMode::Back;
  bool depthTestEnable = true;
  bool depthWriteEnable = true;
  CompareOp depthOp = CompareOp::LessEqual;
  bool blendEnable = false;
  BlendFactor srcBlend = BlendFactor::One;
  BlendFactor dstBlend = BlendFactor::Zero;

  bool operator==(const RenderState &rhs) const {
    return cullMode == rhs.cullMode && depthTestEnable == rhs.depthTestEnable &&
           depthWriteEnable == rhs.depthWriteEnable && depthOp == rhs.depthOp &&
           blendEnable == rhs.blendEnable && srcBlend == rhs.srcBlend &&
           dstBlend == rhs.dstBlend;
  }

  size_t getHash() const {
    size_t h = 0;
    hash_combine(h, static_cast<u32>(cullMode));
    hash_combine(h, depthTestEnable);
    hash_combine(h, depthWriteEnable);
    hash_combine(h, static_cast<u32>(depthOp));
    hash_combine(h, blendEnable);
    hash_combine(h, static_cast<u32>(srcBlend));
    hash_combine(h, static_cast<u32>(dstBlend));
    return h;
  }

  StringID getRenderSignature() const {
    auto &tbl = GlobalStringTable::get();
    StringID fields[] = {
        tbl.Intern(toString(cullMode)),
        tbl.Intern(depthTestEnable ? "DepthTest" : "NoDepthTest"),
        tbl.Intern(depthWriteEnable ? "DepthWrite" : "NoDepthWrite"),
        tbl.Intern(toString(depthOp)),
        tbl.Intern(blendEnable ? "Blend" : "NoBlend"),
        tbl.Intern(toString(srcBlend)),
        tbl.Intern(toString(dstBlend)),
    };
    return tbl.compose(TypeTag::RenderState, fields);
  }
};

/*
@source_analysis.section MaterialPassDefinition：把“单个 pass 的结构”收拢成一个对象
`MaterialPassDefinition` 是 `MaterialTemplate` 里的最小结构单元。它把一个 pass
需要稳定共享的三类信息绑在一起：

- `renderState`：固定功能状态
- `shaderProgram`：shader 名称、variant 组合和编译结果

这个类型的重点不是“保存很多字段”，而是给 template 一个明确的 pass 边界。
只要 `MaterialTemplate` 按 pass 持有它，外层代码就能把“Forward/Shadow/... 的结构差异”
收敛成统一接口，而不用分别管理 render state 和 shader 程序选择。
*/
struct MaterialPassDefinition {
  RenderState renderState;
  ShaderProgramSet shaderProgram;

  size_t getHash() const {
    size_t h = renderState.getHash();
    hash_combine(h, shaderProgram.getHash());
    return h;
  }

  StringID getRenderSignature() const {
    StringID fields[] = {
        shaderProgram.getRenderSignature(),
        renderState.getRenderSignature(),
    };
    return GlobalStringTable::get().compose(TypeTag::MaterialPassDefinition,
                                            fields);
  }
};

} // namespace LX_core
