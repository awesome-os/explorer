#pragma once

#include "fs/file_error.h"

#include <optional>
#include <string>
#include <string_view>

namespace luciole {
  // The error's own sentence, or generic text for its kind when it has none.
  std::string fileErrorText(const FileError& error);
  // Empty when there is no error.
  std::string fileErrorText(const std::optional<FileError>& error);
  // Rename and create name conflicts, length, and access phrased for a name editor.
  std::string
  nameErrorText(const FileError& error, std::string_view missing = "The item or its folder is no longer available.");
} // namespace luciole
