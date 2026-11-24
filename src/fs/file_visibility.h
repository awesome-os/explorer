#pragma once

#include "fs/file_metadata.h"

#include <string_view>

namespace luciole {
  // Match operation-name bytes, independently of dot-file visibility or display labels.
  // Real directories named with a trailing tilde remain normal navigation roots.
  inline bool isEditorBackup(std::string_view name, FileType type) {
    return type != FileType::Directory && name.ends_with('~');
  }
} // namespace luciole
