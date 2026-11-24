#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace luciole {
  enum class AccountKind : std::uint8_t { User, Group };

  // Worker-only passwd/group lookups; failures throw FileErrorException.
  // Empty when the database has no entry for id.
  std::string accountName(std::uint32_t id, AccountKind kind);
  // A name or numeric ID. userRole names a missing user account in the error ("Owner", "User").
  std::uint32_t accountId(const std::string& text, AccountKind kind, std::string_view userRole = "Owner");
  // The effective group or one of the supplementary groups.
  bool memberOfGroup(std::uint32_t group);
} // namespace luciole
