#pragma once

#include "fs/location.h"

#include <optional>
#include <string>

namespace luciole {
  struct TrashInfo {
    std::optional<Location> original;
    std::optional<std::string> deletionDate;
  };
  std::string trashOriginalParentText(const TrashInfo& info);
} // namespace luciole
