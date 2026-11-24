#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace luciole {
  struct ConfigIssue {
    std::filesystem::path file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string key;
    std::string message;
    std::string format() const;
  };

  using ConfigIssues = std::vector<ConfigIssue>;
} // namespace luciole
