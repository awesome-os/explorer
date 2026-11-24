#pragma once

#include "browser/directory_model.h"

#include <chrono>

namespace luciole {
  std::optional<std::string> selectionPatternProblem(const std::string& pattern);
  // Append-only, bounded input for the file pane's optional typeahead mode.
  // The clock is explicit so timeout, repeat cycling and edits are testable.
  class TypeaheadSelection {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto timeout = std::chrono::milliseconds(1200);
    std::optional<EntryId>
    append(DirectoryModel& model, const std::string& text, Clock::time_point now, bool composing = false);
    std::optional<EntryId>
    erase(DirectoryModel& model, std::uint32_t bytes, Clock::time_point now, bool composing = false);
    void clear();
    bool expire(Clock::time_point now);
    const std::string& text() const { return m_text; }

  private:
    std::optional<EntryId> match(DirectoryModel& model, bool cycle);
    std::string m_text;
    Clock::time_point m_changed;
  };
} // namespace luciole
