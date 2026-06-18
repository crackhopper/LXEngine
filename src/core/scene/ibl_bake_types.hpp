#pragma once

#include "core/platform/types.hpp"

#include <string>

namespace LX_core {

using BakeJobId = u64;
using BakeItemId = u64;

enum class IblBakeJobPhase {
  Queued,
  CacheCheck,
  Filter,
  WriteCache,
  ItemComplete,
  Activate,
  Complete,
  Failed,
  ActivationFailed,
  CancelPending,
};

enum class IblBakeJobSeverity {
  Info,
  Warning,
  Error,
};

struct IblBakeJobEvent final {
  BakeJobId job = 0;
  BakeItemId item = 0;
  IblBakeJobPhase phase = IblBakeJobPhase::Queued;
  IblBakeJobSeverity severity = IblBakeJobSeverity::Info;
  float progress = 0.0f;
  std::string message;
  std::string fix;
  u64 sequence = 0;
};

} // namespace LX_core
