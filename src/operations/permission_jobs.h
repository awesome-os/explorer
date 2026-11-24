#pragma once

#include "fs/cancel_token.h"
#include "fs/file_error.h"
#include "operations/permission_service.h"
#include "operations/permission_target.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Worker-side permission jobs. Each returns its outcome for the main thread to publish;
// an unexpected failure throws FileErrorException.
namespace luciole {
  struct PermissionReply {
    std::shared_ptr<PermissionTarget> target = nullptr;
    std::optional<PermissionInfo> info = std::nullopt;
    std::string message = {}; // NOLINT(readability-redundant-member-init): replies use designated initializers.
    std::optional<FileError> error = std::nullopt;
    bool itemsFailed = false, applied = false;
    std::shared_ptr<PermissionBatch> bulk = nullptr;
    std::shared_ptr<std::vector<PermissionResult>> results = nullptr;
    bool finished = false;
    std::string report = {}; // NOLINT(readability-redundant-member-init)
  };

  // names resolves owner and group display names after the change.
  PermissionReply applyPermissionEdit(
      const std::shared_ptr<PermissionTarget>& target, const PermissionEdit& edit, const CancelToken& token,
      bool names = true
  );
  PermissionReply applyAclEdit(const std::shared_ptr<PermissionTarget>& target, AclEdit edit, const CancelToken& token);
  PermissionReply applyBulkPermissionEdit(
      const std::shared_ptr<PermissionBatch>& bulk, PermissionEdit edit, const CancelToken& token,
      std::atomic<std::size_t>& completed
  );
  PermissionReply applyBulkAclPatch(
      const std::shared_ptr<PermissionBatch>& bulk, AclPatchEdit edit, const CancelToken& token,
      std::atomic<std::size_t>& completed
  );
  // Collects the roots' trees first, then applies the edit to every collected item.
  PermissionReply applyRecursivePermissionEdit(
      const std::shared_ptr<PermissionBatch>& roots, RecursivePermissionEdit edit, const CancelToken& token,
      std::atomic<std::size_t>& completed, std::atomic<std::size_t>& total, std::atomic<bool>& collecting
  );
  std::string permissionReport(const std::vector<PermissionResult>& rows);
} // namespace luciole
