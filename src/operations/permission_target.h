#pragma once

#include "fs/directory_session.h"
#include "fs/posix_fd.h"
#include "operations/permission_service.h"

#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace luciole {
  bool samePermissions(const struct stat& a, const struct stat& b);

  // A captured native item held by an O_PATH descriptor. Worker-only.
  struct PermissionTarget {
    DirectoryEntry entry;
    std::string path;
    Fd fd;
    struct stat baseline;
    bool identified = false;
    std::vector<std::shared_ptr<PermissionTarget>> ancestors;
    std::optional<AclInfo> aclBaseline;
    std::shared_ptr<const AclInfo> aclView;

    // Opens the entry's native path without following a final link.
    explicit PermissionTarget(DirectoryEntry entry);
    // Adopts a descriptor opened below an ancestor; the descriptor identifies the item.
    PermissionTarget(DirectoryEntry entry, int descriptor, std::vector<std::shared_ptr<PermissionTarget>> ancestors);

    // The path still names the held inode.
    bool current() const;
    // Throws a Changed or Unsupported FileErrorException when an edit must not proceed.
    void validate() const;
    PermissionInfo inspect(bool names = true) const;
    // adopt makes the inspected ACL the edit baseline.
    void loadAcl(bool adopt);
  };

  // Rows and their targets by index; a failed or unvisited row has no target.
  struct PermissionBatch {
    std::vector<std::shared_ptr<PermissionTarget>> targets;
    std::shared_ptr<std::vector<PermissionResult>> results;
  };

  // Opens and identifies a native item outside Trash; throws FileErrorException.
  std::shared_ptr<PermissionTarget> preparePermissionTarget(const DirectoryEntry& entry, bool captureCurrentLocation);
} // namespace luciole
