#pragma once

#include "core/scene/ibl_bake_types.hpp"

#include <mutex>
#include <string_view>
#include <vector>

namespace LX_core {

[[nodiscard]] std::string_view iblBakeJobPhaseName(IblBakeJobPhase phase);
[[nodiscard]] std::string_view
iblBakeJobSeverityName(IblBakeJobSeverity severity);

class IblBakeEventQueue final {
public:
  [[nodiscard]] IblBakeJobEvent push(IblBakeJobEvent event);
  [[nodiscard]] std::vector<IblBakeJobEvent> drainSince(u64 sequence) const;

private:
  mutable std::mutex m_mutex;
  u64 m_nextSequence = 1;
  std::vector<IblBakeJobEvent> m_events;
};

} // namespace LX_core
