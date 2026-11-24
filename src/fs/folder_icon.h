#pragma once

#include "fs/location.h"

#include <optional>
#include <string>

namespace luciole {
  // GVFS metadata keys GNOME Files uses for per-folder artwork: custom-icon holds a file:// URI to an image,
  // custom-icon-name an icon-theme name. Reading them is customFolderIcon's (fs/custom_icons.h).
  inline constexpr const char* kFolderCustomIconAttribute = "metadata::custom-icon";
  inline constexpr const char* kFolderCustomIconNameAttribute = "metadata::custom-icon-name";

  // A local folder's custom icon name or image path, for preselecting the picker. Blocking: call on a worker.
  std::optional<std::string> readFolderCustomIcon(const Location& folder);
  // Stores the icon: a theme name goes to metadata::custom-icon-name, an image file to metadata::custom-icon as a
  // file:// URI (the other key is cleared so GNOME Files never sees a stale value), plus a .directory entry for
  // local folders. Blocking: call on a worker.
  bool setFolderCustomIcon(const Location& folder, const std::string& iconName, std::string& error);
  // Clears both metadata keys best-effort; removes a .directory left without keys. Blocking: call on a worker.
  bool clearFolderCustomIcon(const Location& folder, std::string& error);
} // namespace luciole
