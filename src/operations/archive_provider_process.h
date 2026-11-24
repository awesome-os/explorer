#pragma once

#include "operations/archive_input.h"

namespace luciole::archive_work {
  // Source fd 3 and password stdin are private to the bounded child. argv is NUL-terminated.
  // Child stdout is copied to output up to outputLimit; fileLimit bounds each file the child writes.
  int runArchiveProvider(
      Input& input, Work& work, std::string_view password, std::span<const char* const> argv, int output,
      std::uint64_t outputLimit, std::uint64_t fileLimit
  );
} // namespace luciole::archive_work
