#pragma once

#include "fs/cancel_token.h"
#include "operations/permission_target.h"

#include <atomic>
#include <cstddef>

namespace luciole {
  // Appends each root and its descendants to plan in postorder, so parent access stays
  // intact while descendants change. Links, other mounts and repeated folders become
  // Skipped rows; unreadable items become Failed rows. Selected roots found below
  // another root are visited once, through that walk. Stops early when token is
  // cancelled. Throws FileErrorException when the job exceeds its item, depth or
  // descriptor limits, leaving the rows collected so far in plan.
  void collectPermissionTargets(
      const PermissionBatch& roots, PermissionBatch& plan, const CancelToken& token, std::atomic<std::size_t>& found
  );
} // namespace luciole
