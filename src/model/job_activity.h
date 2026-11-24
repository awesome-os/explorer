#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace luciole {
  enum class ActivityState { Preparing, Working, Waiting, Succeeded, Failed, Partial, Cancelled };
  struct JobActivity {
    std::uint64_t id = 0;
    std::string title, summary;
    ActivityState state = ActivityState::Preparing;
    std::optional<float> progress;
    std::shared_ptr<const std::string> report;
    bool finished = false, canCancel = false, canRetry = false, canReview = false;
    bool failed() const { return state == ActivityState::Failed || state == ActivityState::Partial; }
  };
} // namespace luciole
