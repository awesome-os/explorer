#include "operations/posix_acl.h"

#include "fs/file_error.h"
#include "fs/posix_fd.h"

#include <acl/libacl.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <limits>
#include <memory>
#include <set>
#include <sys/acl.h>
#include <sys/stat.h>
#include <tuple>
#include <type_traits>

namespace {
  struct FreeAcl {
    void operator()(void* value) const { ::acl_free(value); }
  };
  using Acl = std::unique_ptr<std::remove_pointer_t<acl_t>, FreeAcl>;
  constexpr std::array<acl_tag_t, 6> tags{ACL_USER_OBJ, ACL_USER, ACL_GROUP_OBJ, ACL_GROUP, ACL_MASK, ACL_OTHER};
  std::string path(int fd) {
    const auto value = luciole::statFd(fd);
    if (!S_ISREG(value.st_mode) && !S_ISDIR(value.st_mode)) {
      luciole::throwFileError(
          luciole::FileErrorKind::Unsupported,
          "ACL editing supports native regular files and folders, without following links."
      );
    }
    return "/proc/self/fd/" + std::to_string(fd);
  }
  void check(int result) {
    if (result != 0) {
      luciole::throwErrno(errno);
    }
  }
  [[noreturn]] void invalid(std::string message) {
    luciole::throwFileError(luciole::FileErrorKind::InvalidArgument, std::move(message));
  }
  std::uint32_t numeric(const std::string& text) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty()
        || parsed.ec != std::errc{}
        || parsed.ptr != text.data() + text.size()
        || value == std::numeric_limits<std::uint32_t>::max()) {
      invalid("Invalid ACL account ID.");
    }
    return value;
  }
  void sort(std::vector<luciole::AclEntry>& entries) {
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
      const auto id = [](const auto& entry) { return entry.qualifier.empty() ? 0U : numeric(entry.qualifier); };
      return std::tuple{left.tag, id(left)} < std::tuple{right.tag, id(right)};
    });
  }
  Acl build(const std::vector<luciole::AclEntry>& entries) {
    if (entries.size() > luciole::maxAclEntries) {
      invalid("The ACL editor supports at most 128 entries per list.");
    }
    Acl acl(::acl_init(static_cast<int>(entries.size())));
    if (!acl) {
      luciole::throwErrno(errno);
    }
    for (const auto& value : entries) {
      const auto tag = static_cast<std::size_t>(value.tag);
      if (tag >= tags.size() || value.permissions > 7) {
        invalid("Invalid ACL tag or permissions.");
      }
      const bool named = value.tag == luciole::AclTag::User || value.tag == luciole::AclTag::Group;
      if (!named && !value.qualifier.empty()) {
        invalid("Only named user/group entries accept an account.");
      }
      acl_entry_t entry = nullptr;
      auto* raw = acl.release();
      const int added = ::acl_create_entry(&raw, &entry);
      acl.reset(raw);
      check(added);
      check(::acl_set_tag_type(entry, tags[tag]));
      if (named) {
        const auto id = numeric(value.qualifier);
        if (value.tag == luciole::AclTag::User) {
          const uid_t user = id;
          check(::acl_set_qualifier(entry, &user));
        } else {
          const gid_t group = id;
          check(::acl_set_qualifier(entry, &group));
        }
      }
      acl_permset_t permissions = nullptr;
      check(::acl_get_permset(entry, &permissions));
      check(::acl_clear_perms(permissions));
      for (const auto bit : {ACL_READ, ACL_WRITE, ACL_EXECUTE}) {
        if ((value.permissions & bit) != 0) {
          check(::acl_add_perm(permissions, bit));
        }
      }
      check(::acl_set_permset(entry, permissions));
    }
    if (!entries.empty() && ::acl_valid(acl.get()) != 0) {
      invalid("Invalid ACL: require owner, owning group and others; named accounts need a mask and unique IDs.");
    }
    return acl;
  }
  std::vector<luciole::AclEntry> read(const std::string& file, acl_type_t type) {
    Acl acl(::acl_get_file(file.c_str(), type));
    if (!acl) {
      luciole::throwErrno(errno);
    }
    const auto count = ::acl_entries(acl.get());
    if (count < 0 || count > static_cast<int>(luciole::maxAclEntries)) {
      luciole::throwFileError(luciole::FileErrorKind::Unsupported, "The ACL exceeds the editor's 128-entry limit.");
    }
    std::vector<luciole::AclEntry> entries;
    acl_entry_t entry = nullptr;
    for (int next = ::acl_get_entry(acl.get(), ACL_FIRST_ENTRY, &entry); next != 0;
         next = ::acl_get_entry(acl.get(), ACL_NEXT_ENTRY, &entry)) {
      if (next < 0) {
        luciole::throwErrno(errno);
      }
      acl_tag_t tag = 0;
      check(::acl_get_tag_type(entry, &tag));
      const auto* const found = std::ranges::find(tags, tag);
      if (found == tags.end()) {
        luciole::throwFileError(luciole::FileErrorKind::Unsupported, "Unsupported ACL entry type.");
      }
      luciole::AclEntry value{static_cast<luciole::AclTag>(found - tags.begin()), {}, 0};
      if (tag == ACL_USER || tag == ACL_GROUP) {
        const std::unique_ptr<void, FreeAcl> id(::acl_get_qualifier(entry));
        if (!id) {
          luciole::throwErrno(errno);
        }
        value.qualifier =
            std::to_string(tag == ACL_USER ? *static_cast<uid_t*>(id.get()) : *static_cast<gid_t*>(id.get()));
      }
      acl_permset_t permissions = nullptr;
      check(::acl_get_permset(entry, &permissions));
      for (const auto bit : {ACL_READ, ACL_WRITE, ACL_EXECUTE}) {
        const int present = ::acl_get_perm(permissions, bit);
        if (present < 0) {
          luciole::throwErrno(errno);
        }
        if (present != 0) {
          value.permissions |= bit;
        }
      }
      entries.push_back(std::move(value));
    }
    sort(entries);
    return entries;
  }
} // namespace
luciole::AclInfo luciole::inspectAcl(int descriptor) {
  AclInfo info;
  try {
    const auto file = path(descriptor);
    info.directory = S_ISDIR(statFd(descriptor).st_mode);
    info.access = read(file, ACL_TYPE_ACCESS);
    if (info.directory) {
      info.defaults = read(file, ACL_TYPE_DEFAULT);
    }
    info.supported = true;
  } catch (const std::exception& error) {
    info.message = "ACL unavailable: " + std::string(error.what());
  }
  return info;
}
std::vector<luciole::AclEntry>
luciole::normalizeAcl(std::vector<AclEntry> entries, bool allowEmpty, const AclResolver& resolve) {
  if (!allowEmpty && entries.empty()) {
    invalid("The access ACL cannot be empty.");
  }
  if (entries.size() > maxAclEntries) {
    invalid("The ACL editor supports at most 128 entries per list.");
  }
  for (auto& entry : entries) {
    if (entry.tag == AclTag::User || entry.tag == AclTag::Group) {
      entry.qualifier = std::to_string(resolve(entry.tag, entry.qualifier));
    }
  }
  sort(entries);
  const auto checked = build(entries);
  return entries;
}
luciole::AclEdit luciole::normalizeAclEdit(AclEdit edit, const AclResolver& resolve) {
  if (edit.access) {
    edit.access = normalizeAcl(std::move(*edit.access), false, resolve);
  }
  if (edit.defaults) {
    edit.defaults = normalizeAcl(std::move(*edit.defaults), true, resolve);
  }
  return edit;
}
void luciole::writeAcl(int descriptor, bool defaults, const std::vector<AclEntry>& entries) {
  const auto file = path(descriptor);
  if (!defaults && entries.empty()) {
    invalid("The access ACL cannot be empty.");
  }
  const auto acl = build(entries);
  check(::acl_set_file(file.c_str(), defaults ? ACL_TYPE_DEFAULT : ACL_TYPE_ACCESS, acl.get()));
}
bool luciole::sameAcl(const AclInfo& left, const AclInfo& right) {
  return left.supported && right.supported && left.access == right.access && left.defaults == right.defaults;
}
bool luciole::satisfiesAcl(const AclInfo& actual, const AclEdit& desired) {
  return actual.supported
      && (!desired.access || actual.access == *desired.access)
      && (!desired.defaults || actual.defaults == *desired.defaults);
}
bool luciole::changesAccess(const AclPatchEdit& edit) {
  return !edit.access.entries.empty() || edit.access.maskPolicy != AclMaskPolicy::Keep;
}
bool luciole::skipsDefaults(const AclInfo& actual, const AclPatchEdit& edit) {
  return !actual.directory || (edit.defaultPolicy == AclDefaultPolicy::Existing && actual.defaults.empty());
}
luciole::AclPatchEdit luciole::normalizeAclPatch(AclPatchEdit edit, const AclResolver& resolve) {
  if (edit.defaultPolicy < AclDefaultPolicy::Keep || edit.defaultPolicy > AclDefaultPolicy::Remove) {
    invalid("Invalid default ACL policy.");
  }
  if ((edit.defaultPolicy == AclDefaultPolicy::Keep || edit.defaultPolicy == AclDefaultPolicy::Remove)
      && (!edit.defaults.entries.empty() || edit.defaults.maskPolicy != AclMaskPolicy::Keep)) {
    invalid("Choose Edit existing or Create before changing default rules.");
  }
  for (auto* list : {&edit.access, &edit.defaults}) {
    if (list->entries.size() > maxAclEntries
        || list->maskPolicy < AclMaskPolicy::Keep
        || list->maskPolicy > AclMaskPolicy::RemoveUnused) {
      invalid("Invalid ACL patch or too many rules (maximum 128 per list).");
    }
    std::set<std::pair<AclTag, std::string>> seen;
    for (auto& entry : list->entries) {
      const bool named = entry.tag == AclTag::User || entry.tag == AclTag::Group;
      if (entry.tag < AclTag::Owner
          || entry.tag > AclTag::Other
          || entry.mask > 7
          || (entry.permissions && (*entry.permissions > 7 || entry.mask == 0))
          || (!entry.permissions && !named)
          || (!named && !entry.qualifier.empty())) {
        invalid("Invalid ACL rule change.");
      }
      if (entry.tag == AclTag::Mask && list->maskPolicy != AclMaskPolicy::Keep) {
        invalid("Choose either mask bit changes or an automatic mask policy.");
      }
      if (named) {
        entry.qualifier = std::to_string(resolve(entry.tag, entry.qualifier));
        (void)numeric(entry.qualifier);
      }
      if (!seen.emplace(entry.tag, entry.qualifier).second) {
        invalid("Each account or ACL role can be changed only once per list.");
      }
    }
  }
  return edit;
}
luciole::AclEdit luciole::patchAcl(const AclInfo& actual, const AclPatchEdit& edit) {
  if (!actual.supported) {
    throwFileError(
        FileErrorKind::Unsupported, actual.message.empty() ? "ACL inspection is unavailable." : actual.message
    );
  }
  const auto patch = [](std::vector<AclEntry> entries, const AclListPatch& changes) {
    for (const auto& change : changes.entries) {
      auto found = std::ranges::find_if(entries, [&](const auto& entry) {
        return entry.tag == change.tag && entry.qualifier == change.qualifier;
      });
      if (!change.permissions) {
        if (found != entries.end()) {
          entries.erase(found);
        }
      } else if (found == entries.end()) {
        const auto group = std::ranges::find(entries, AclTag::OwningGroup, &AclEntry::tag);
        const auto initial = change.tag == AclTag::Mask && group != entries.end() ? group->permissions : 0U;
        entries.push_back(
            {change.tag, change.qualifier, (initial & ~change.mask) | (*change.permissions & change.mask)}
        );
      } else {
        found->permissions = (found->permissions & ~change.mask) | (*change.permissions & change.mask);
      }
    }
    auto mask = std::ranges::find(entries, AclTag::Mask, &AclEntry::tag);
    const bool named = std::ranges::any_of(entries, [](const auto& entry) {
      return entry.tag == AclTag::User || entry.tag == AclTag::Group;
    });
    const auto group = std::ranges::find(entries, AclTag::OwningGroup, &AclEntry::tag);
    if (group == entries.end()) {
      invalid("The owning-group rule is missing.");
    }
    std::uint32_t rights = group->permissions;
    if (changes.maskPolicy == AclMaskPolicy::RemoveUnused) {
      if (named) {
        invalid("Cannot remove the mask while named rules remain.");
      }
      if (mask != entries.end()) {
        entries.erase(mask);
      }
    } else if (changes.maskPolicy == AclMaskPolicy::Recalculate || (named && mask == entries.end())) {
      if (changes.maskPolicy == AclMaskPolicy::Recalculate) {
        for (const auto& entry : entries) {
          if (entry.tag == AclTag::User || entry.tag == AclTag::Group) {
            rights |= entry.permissions;
          }
        }
      }
      if (mask == entries.end()) {
        entries.push_back({AclTag::Mask, {}, rights});
      } else {
        mask->permissions = rights;
      }
    }
    return normalizeAcl(std::move(entries), false, [](AclTag, const std::string& id) { return numeric(id); });
  };
  AclEdit result;
  auto access = actual.access;
  if (changesAccess(edit)) {
    access = patch(access, edit.access);
    if (access != actual.access) {
      result.access = access;
    }
  }
  if (actual.directory && edit.defaultPolicy != AclDefaultPolicy::Keep) {
    auto defaults = actual.defaults;
    if (edit.defaultPolicy == AclDefaultPolicy::Remove) {
      defaults.clear();
    } else if (!defaults.empty() || edit.defaultPolicy == AclDefaultPolicy::Create) {
      defaults = patch(defaults.empty() ? access : defaults, edit.defaults);
    }
    if (defaults != actual.defaults) {
      result.defaults = std::move(defaults);
    }
  }
  return result;
}
std::string luciole::describeAcl(const AclInfo& info) {
  if (!info.supported) {
    return "  " + info.message + "\n";
  }
  const auto permissions = [](std::uint32_t value) {
    std::string text = "---";
    for (std::size_t i = 0; i < 3; ++i) {
      if ((value & (4U >> i)) != 0) {
        text[i] = "rwx"[i];
      }
    }
    return text;
  };
  std::string text;
  for (const bool defaults : {false, true}) {
    if (defaults && !info.directory) {
      continue;
    }
    const auto& entries = defaults ? info.defaults : info.access;
    text += defaults ? "  Default ACL" : "  Access ACL";
    text += entries.empty() ? ": none\n" : ":\n";
    const auto mask = std::ranges::find(entries, AclTag::Mask, &AclEntry::tag);
    for (const auto& entry : entries) {
      constexpr std::array roles{"owner", "user", "owning group", "group", "mask", "other"};
      text += "    "
          + std::string(roles.at(static_cast<std::size_t>(entry.tag)))
          + ":"
          + entry.qualifier
          + ":"
          + permissions(entry.permissions);
      if (mask != entries.end()
          && (entry.tag == AclTag::User || entry.tag == AclTag::OwningGroup || entry.tag == AclTag::Group)) {
        text += " effective:" + permissions(entry.permissions & mask->permissions);
      }
      text += '\n';
    }
  }
  return text;
}
