#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace luciole {
  std::optional<std::string> filenameProblem(const std::string& name);
  std::size_t filenameStemLength(const std::string& name, bool directory);
  // Filename bytes as valid UTF-8 presentation text.
  std::string filenameDisplayText(const std::string& name);
} // namespace luciole
