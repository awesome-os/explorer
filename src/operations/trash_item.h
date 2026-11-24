#pragma once

#include "fs/cancel_token.h"
#include "fs/directory_session.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace luciole {
  struct TrashItemIdentity {
    FileIdentity id;
    FileType type = FileType::Unknown;
    bool symlink = false;
    std::optional<std::string> etag, original, deleted;
    bool operator==(const TrashItemIdentity&) const = default;
  };

  // What one provider query reported about an item.
  struct TrashItemInfo {
    std::optional<FileIdentity> id;
    FileType type = FileType::Unknown;
    bool symlink = false;
    std::optional<std::uint64_t> size, modified;
    std::optional<std::string> etag, original, deleted;
    // Throws when the item cannot be identified safely.
    TrashItemIdentity identity() const;
  };

  struct TrashItem {
    Location location;
    std::string displayName;
    TrashItemIdentity identity;
  };

  bool isTopLevelTrashItem(const Location& location);
  TrashItemIdentity readTrashItemIdentity(const DirectoryEntry& entry);
  // Blocking; throws FileErrorException. follow resolves a symlink to its target.
  TrashItemInfo queryTrashItem(const Location& location, const CancelToken& token, bool follow = false);
  // Blocking; the immediate items of the Trash root. Throws FileErrorException.
  std::vector<TrashItem> listTrashItems(const CancelToken& token);
} // namespace luciole
