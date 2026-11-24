#include "operations/permission_accounts.h"

#include "fs/file_error.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <format>
#include <grp.h>
#include <limits>
#include <optional>
#include <pwd.h>
#include <unistd.h>
#include <vector>

namespace {
  template <typename Record, typename Lookup> std::optional<Record> account(std::vector<char>& buffer, Lookup lookup) {
    for (std::size_t size = 4096; size <= 1024 * 1024; size *= 2) {
      buffer.resize(size);
      Record record{}, *result = nullptr;
      const int code = lookup(record, buffer, result);
      if (code == ERANGE) {
        continue;
      }
      if (code != 0) {
        const auto error = luciole::errnoError(code);
        luciole::throwFileError(error.kind, "Account lookup failed: " + error.message);
      }
      if (result) {
        return record;
      }
      return std::nullopt;
    }
    luciole::throwFileError(luciole::FileErrorKind::Other, "Account lookup exceeded its size limit.");
  }
} // namespace

std::string luciole::accountName(std::uint32_t id, AccountKind kind) {
  std::vector<char> buffer;
  if (kind == AccountKind::User) {
    const auto record = account<passwd>(buffer, [id](auto& record, auto& bytes, auto*& result) {
      return ::getpwuid_r(id, &record, bytes.data(), bytes.size(), &result);
    });
    if (record) {
      return record->pw_name;
    }
  } else {
    const auto record = account<group>(buffer, [id](auto& record, auto& bytes, auto*& result) {
      return ::getgrgid_r(id, &record, bytes.data(), bytes.size(), &result);
    });
    if (record) {
      return record->gr_name;
    }
  }
  return {};
}

std::uint32_t luciole::accountId(const std::string& text, AccountKind kind, std::string_view userRole) {
  if (text.empty() || text.size() > 1024 || text.contains('\0')) {
    throwFileError(FileErrorKind::InvalidArgument, "Enter an account name or a numeric ID.");
  }
  if (std::ranges::all_of(text, [](unsigned char c) { return c >= '0' && c <= '9'; })) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || value == std::numeric_limits<std::uint32_t>::max()) {
      throwFileError(FileErrorKind::InvalidArgument, "Account ID is out of range.");
    }
    return value;
  }
  std::vector<char> buffer;
  if (kind == AccountKind::User) {
    const auto record = account<passwd>(buffer, [&text](auto& record, auto& bytes, auto*& result) {
      return ::getpwnam_r(text.c_str(), &record, bytes.data(), bytes.size(), &result);
    });
    if (record && record->pw_uid != static_cast<uid_t>(-1)) {
      return record->pw_uid;
    }
    throwFileError(FileErrorKind::Missing, std::format("{} account was not found.", userRole));
  }
  const auto record = account<group>(buffer, [&text](auto& record, auto& bytes, auto*& result) {
    return ::getgrnam_r(text.c_str(), &record, bytes.data(), bytes.size(), &result);
  });
  if (record && record->gr_gid != static_cast<gid_t>(-1)) {
    return record->gr_gid;
  }
  throwFileError(FileErrorKind::Missing, "Group was not found.");
}

bool luciole::memberOfGroup(std::uint32_t group) {
  if (group == ::getegid()) {
    return true;
  }
  const int count = ::getgroups(0, nullptr);
  if (count < 0) {
    throwErrno(errno);
  }
  std::vector<gid_t> groups(static_cast<std::size_t>(count));
  if (::getgroups(count, groups.data()) < 0) {
    throwErrno(errno);
  }
  return std::ranges::find(groups, group) != groups.end();
}
