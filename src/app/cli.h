#pragma once

#include <span>
#include <string_view>

namespace luciole {

  int runCli(std::span<const std::string_view> args);

} // namespace luciole
