#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace luciole {
  enum class AclTag { Owner, User, OwningGroup, Group, Mask, Other };
  struct AclEntry {
    AclTag tag;
    std::string qualifier; // Numeric UID/GID on inspection; names or IDs in an edit.
    std::uint32_t permissions = 0;
    bool operator==(const AclEntry&) const = default;
  };
  struct AclInfo {
    std::vector<AclEntry> access, defaults;
    bool supported = false, editable = false, directory = false;
    std::string message;
  };
  struct AclEdit {
    std::optional<std::vector<AclEntry>> access, defaults;
  };
  enum class AclMaskPolicy { Keep, Recalculate, RemoveUnused };
  enum class AclDefaultPolicy { Keep, Existing, Create, Remove };
  struct AclEntryPatch {
    AclTag tag;
    std::string qualifier;
    std::optional<std::uint32_t> permissions; // No value removes a named rule.
    std::uint32_t mask = 7;                   // Only these rwx bits change; a new rule starts with no rights.
  };
  struct AclListPatch {
    std::vector<AclEntryPatch> entries;
    AclMaskPolicy maskPolicy = AclMaskPolicy::Keep;
  };
  struct AclPatchEdit {
    AclListPatch access, defaults;
    AclDefaultPolicy defaultPolicy = AclDefaultPolicy::Keep;
  };
  inline constexpr std::size_t maxAclEntries = 128;
  // Maps a named rule's account text to its numeric UID/GID.
  using AclResolver = std::function<std::uint32_t(AclTag, const std::string&)>;
  // Worker-only Linux POSIX ACL adapter. Descriptor paths always address a held,
  // non-symlink inode; callers retain pathname/metadata validation policy.
  // inspectAcl reports failure in AclInfo::message; the others throw FileErrorException.
  AclInfo inspectAcl(int descriptor);
  std::vector<AclEntry> normalizeAcl(std::vector<AclEntry> entries, bool allowEmpty, const AclResolver& resolve);
  // Validates both tables of an edit before either can be written.
  AclEdit normalizeAclEdit(AclEdit edit, const AclResolver& resolve);
  void writeAcl(int descriptor, bool defaults, const std::vector<AclEntry>& entries);
  bool sameAcl(const AclInfo& left, const AclInfo& right);
  // actual holds every table that desired sets.
  bool satisfiesAcl(const AclInfo& actual, const AclEdit& desired);
  bool changesAccess(const AclPatchEdit& edit);
  // The patch leaves this item's default table alone: a file, or Edit existing on a folder without one.
  bool skipsDefaults(const AclInfo& actual, const AclPatchEdit& edit);
  AclPatchEdit normalizeAclPatch(AclPatchEdit edit, const AclResolver& resolve);
  // Apply a normalized patch to this item's tables, preserving all unmentioned rules.
  AclEdit patchAcl(const AclInfo& actual, const AclPatchEdit& edit);
  std::string describeAcl(const AclInfo& info);
} // namespace luciole
