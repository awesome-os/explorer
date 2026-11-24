#include "config/diagnostic.h"

#include <format>

std::string luciole::ConfigIssue::format() const {
  auto location = file.string();
  if (line != 0) {
    location += std::format(":{}:{}", line, column);
  }
  return key.empty() ? std::format("{}: {}", location, message) : std::format("{}: {}: {}", location, key, message);
}
