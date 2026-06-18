#include "core/scene/ibl_bake_job.hpp"

#include <utility>

namespace LX_core {

std::string_view iblBakeJobPhaseName(IblBakeJobPhase phase) {
  switch (phase) {
  case IblBakeJobPhase::Queued:
    return "queued";
  case IblBakeJobPhase::CacheCheck:
    return "cache-check";
  case IblBakeJobPhase::Filter:
    return "filter";
  case IblBakeJobPhase::WriteCache:
    return "write-cache";
  case IblBakeJobPhase::ItemComplete:
    return "item-complete";
  case IblBakeJobPhase::Activate:
    return "activate";
  case IblBakeJobPhase::Complete:
    return "complete";
  case IblBakeJobPhase::Failed:
    return "failed";
  case IblBakeJobPhase::ActivationFailed:
    return "activation-failed";
  case IblBakeJobPhase::CancelPending:
    return "cancel-pending";
  }
  return "unknown";
}

std::string_view iblBakeJobSeverityName(IblBakeJobSeverity severity) {
  switch (severity) {
  case IblBakeJobSeverity::Info:
    return "info";
  case IblBakeJobSeverity::Warning:
    return "warning";
  case IblBakeJobSeverity::Error:
    return "error";
  }
  return "unknown";
}

IblBakeJobEvent IblBakeEventQueue::push(IblBakeJobEvent event) {
  std::lock_guard lock(m_mutex);
  event.sequence = m_nextSequence++;
  m_events.push_back(event);
  return event;
}

std::vector<IblBakeJobEvent>
IblBakeEventQueue::drainSince(u64 sequence) const {
  std::lock_guard lock(m_mutex);
  std::vector<IblBakeJobEvent> out;
  for (const IblBakeJobEvent &event : m_events) {
    if (event.sequence > sequence) {
      out.push_back(event);
    }
  }
  return out;
}

} // namespace LX_core
