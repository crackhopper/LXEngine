#pragma once

#include "core/utils/string_table.hpp"

namespace LX_core {

/// PipelineKey 是 pipeline identity 的句柄：一个结构化 `StringID`，
/// 由 `GlobalStringTable::compose(TypeTag::PipelineKey, {objSig, matSig})`
/// 生成。 调用 `GlobalStringTable::toDebugString(key.id)`
/// 可以还原出完整的人类可读 pipeline tree，用于日志和调试断言。
struct PipelineKey {
  StringID id;

  bool operator==(const PipelineKey &rhs) const { return id == rhs.id; }
  bool operator!=(const PipelineKey &rhs) const { return id != rhs.id; }

  struct Hash {
    size_t operator()(const PipelineKey &k) const {
      return StringID::Hash{}(k.id);
    }
  };

  /// 两级 compose：object signature + material signature。调用方通过
  /// `IRenderable::getRenderSignature(pass)` 与
  /// `IMaterial::getRenderSignature(pass)` 先各自组装结构化签名，再传入本函数。
  static PipelineKey build(StringID objectSig, StringID materialSig);
};

} // namespace LX_core
