#include "operations/permission_jobs.h"

#include "operations/permission_accounts.h"
#include "operations/permission_walk.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <format>
#include <map>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  using namespace luciole;
  using O = PermissionOutcome;
  using Reply = PermissionReply;
  FileError withPrefix(std::string_view prefix, FileError error) {
    error.message.insert(0, prefix);
    return error;
  }
  void appendNote(std::string& message, std::string_view note) {
    if (!message.empty()) {
      message += ' ';
    }
    message += note;
  }
  std::string resultText(const PermissionResult& row) {
    std::string text = row.error ? row.error->message : "";
    if (!row.message.empty()) {
      appendNote(text, row.message);
    }
    return text;
  }
  std::uint32_t aclAccountId(AclTag tag, const std::string& text) {
    return accountId(text, tag == AclTag::User ? AccountKind::User : AccountKind::Group, "User");
  }
  void checkModeBits(std::uint32_t mask, const std::optional<std::uint32_t>& mode) {
    if ((mask & ~07777U) != 0 || (mode && (*mode & ~07777U) != 0)) {
      throwFileError(FileErrorKind::InvalidArgument, "Permission bits are out of range.");
    }
  }
  // Replaces owner and group names with numeric IDs. reaches(gid) tells whether a
  // group change would reach any item, so non-members are refused before any change.
  template <typename Reaches> void resolveAccounts(PermissionEdit& edit, Reaches reaches) {
    if (edit.owner) {
      edit.owner = std::to_string(accountId(*edit.owner, AccountKind::User));
    }
    if (edit.group) {
      const auto group = accountId(*edit.group, AccountKind::Group);
      if (::geteuid() != 0 && !memberOfGroup(group) && reaches(group)) {
        throwFileError(FileErrorKind::Denied, "You must be a member of the requested group.");
      }
      edit.group = std::to_string(group);
    }
  }
} // namespace

luciole::PermissionReply luciole::applyPermissionEdit(
    const std::shared_ptr<PermissionTarget>& target, const PermissionEdit& edit, const CancelToken& token, bool names
) {
  Reply reply{.target = target};
  const auto before = statFd(target->fd.get());
  bool mutated = false;
  try {
    target->validate();
    const auto info = target->inspect(names);
    const uid_t owner = edit.owner ? accountId(*edit.owner, AccountKind::User) : before.st_uid;
    const gid_t group = edit.group ? accountId(*edit.group, AccountKind::Group) : before.st_gid;
    checkModeBits(edit.mask, edit.mode);
    if (edit.mode && (info.symlink || (((*edit.mode ^ before.st_mode) & edit.mask) != 0 && !info.canMode))) {
      if (info.symlink) {
        throwFileError(FileErrorKind::Unsupported, "Link permissions cannot be edited.");
      }
      throwFileError(FileErrorKind::Denied, "Permission changes are not permitted.");
    }
    if ((owner != before.st_uid && !info.canOwner) || (group != before.st_gid && !info.canGroup)) {
      throwFileError(FileErrorKind::Denied, "Ownership changes are not permitted.");
    }
    if (group != before.st_gid && ::geteuid() != 0 && !memberOfGroup(group)) {
      throwFileError(FileErrorKind::Denied, "You must be a member of the requested group.");
    }
    target->validate();
    if (!token.cancelled() && (owner != before.st_uid || group != before.st_gid)) {
      if (::fchownat(
              target->fd.get(), "", owner == before.st_uid ? static_cast<uid_t>(-1) : owner,
              group == before.st_gid ? static_cast<gid_t>(-1) : group, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW
          )
          != 0) {
        throwErrno(errno);
      }
      target->baseline = statFd(target->fd.get());
      mutated = true;
    }
    if (!token.cancelled() && edit.mode && ((*edit.mode ^ statFd(target->fd.get()).st_mode) & edit.mask) != 0) {
      target->validate();
      const auto desired = (statFd(target->fd.get()).st_mode & 07777 & ~edit.mask) | (*edit.mode & edit.mask);
      // The procfs magic link addresses the held non-link inode, never a replacement pathname.
      if (::chmod(("/proc/self/fd/" + std::to_string(target->fd.get())).c_str(), desired) != 0) {
        throwErrno(errno);
      }
      mutated = true;
    }
    const auto after = statFd(target->fd.get());
    reply.applied = mutated && !samePermissions(before, after);
    if (!token.cancelled()
        && (after.st_uid != owner
            || after.st_gid != group
            || (edit.mode && ((after.st_mode ^ *edit.mode) & edit.mask) != 0))) {
      throwFileError(
          FileErrorKind::Unsupported, "The filesystem did not retain all requested permissions or ownership."
      );
    }
    reply.message = token.cancelled() ? "Cancelled. Completed changes remain applied."
        : reply.applied               ? "Changes saved."
                                      : "No changes.";
    target->baseline = after;
  } catch (const std::exception&) {
    reply.applied = mutated && !samePermissions(before, statFd(target->fd.get()));
    const auto error = currentFileError();
    reply.error = reply.applied ? withPrefix("Some changes were applied. ", error) : error;
    // Stale edits require reopening; do not silently adopt another writer's baseline.
  }
  if (target->aclBaseline) {
    target->loadAcl(!reply.error);
  }
  reply.info = target->inspect(names);
  if (!samePermissions(statFd(target->fd.get()), target->baseline)
      || (target->aclBaseline
          && target->aclBaseline->supported
          && target->aclView
          && !sameAcl(*target->aclBaseline, *target->aclView))) {
    reply.info->canMode = reply.info->canOwner = reply.info->canGroup = false;
    reply.info->reason = "Permissions or ownership changed elsewhere. Reopen Properties.";
  }
  return reply;
}

luciole::PermissionReply
luciole::applyAclEdit(const std::shared_ptr<PermissionTarget>& target, AclEdit edit, const CancelToken& token) {
  Reply reply{.target = target};
  bool mutated = false;
  try {
    target->validate();
    if (!target->aclView || !target->aclView->editable || !target->aclBaseline) {
      throwFileError(FileErrorKind::Unavailable, "ACL editing is unavailable for this captured item.");
    }
    if (edit.defaults && !S_ISDIR(target->baseline.st_mode)) {
      throwFileError(FileErrorKind::InvalidArgument, "Only folders support a default ACL.");
    }
    // Masks are explicit.
    edit = normalizeAclEdit(std::move(edit), aclAccountId);
    for (const bool defaults : {false, true}) {
      const auto& wanted = defaults ? edit.defaults : edit.access;
      if (!wanted || token.cancelled()) {
        continue;
      }
      target->validate();
      const auto& previous = defaults ? target->aclBaseline->defaults : target->aclBaseline->access;
      if (*wanted == previous) {
        continue;
      }
      writeAcl(target->fd.get(), defaults, *wanted);
      mutated = true;
      const auto actual = inspectAcl(target->fd.get());
      // Update only our committed metadata. Never retry a failed second write.
      target->baseline = statFd(target->fd.get());
      if (!actual.supported) {
        throwFileError(FileErrorKind::Unsupported, actual.message);
      }
      target->aclBaseline = actual;
      if ((defaults ? actual.defaults : actual.access) != *wanted) {
        throwFileError(FileErrorKind::Unsupported, "The filesystem did not retain the requested ACL.");
      }
    }
    const bool complete = (!edit.access || *edit.access == target->aclBaseline->access)
        && (!edit.defaults || *edit.defaults == target->aclBaseline->defaults);
    const bool cancelled = token.cancelled() && !complete;
    std::string message = cancelled
        ? (mutated ? "Cancelled after applying some ACL changes. Completed changes remain applied."
                   : "Cancelled before applying ACL changes.")
        : mutated ? "Access control saved."
                  : "No changes.";
    if (mutated && !complete) {
      reply.error = FileError{cancelled ? FileErrorKind::Cancelled : FileErrorKind::Other, std::move(message)};
    } else {
      reply.message = std::move(message);
    }
  } catch (const std::exception&) {
    const auto error = currentFileError();
    reply.error = mutated ? withPrefix("Some ACL changes were applied. ", error) : error;
  }
  reply.applied = mutated;
  target->loadAcl(false);
  reply.info = target->inspect();
  return reply;
}

std::string luciole::permissionReport(const std::vector<PermissionResult>& rows) {
  constexpr std::array labels{"Ready", "Changed", "Unchanged", "Failed", "Partially changed", "Cancelled", "Skipped"};
  std::string text = "Permission results\n";
  for (const auto& row : rows) {
    if (text.size() > 16 * 1024 * 1024) {
      text += "\nReport truncated. All results remain available in Properties.\n";
      break;
    }
    text += row.location.uri() + "\n  " + labels[static_cast<std::size_t>(row.outcome)];
    if (row.info) {
      text += std::format(" · mode {:04o} · owner {} · group {}", row.info->mode, row.info->owner, row.info->group);
    }
    if (const auto detail = resultText(row); !detail.empty()) {
      text += "\n  " + detail;
    }
    text += '\n';
    if (row.info && row.info->acl) {
      text += describeAcl(*row.info->acl);
    }
  }
  return text;
}

namespace {
  // The loop shared by bulk mode and ACL edits. Action supplies validation, the
  // per-item change, and the post-pass that reports effects on unvisited aliases.
  template <typename Action>
  Reply runBulk(
      const std::shared_ptr<PermissionBatch>& bulk, Action& action, const CancelToken& token,
      std::atomic<std::size_t>& completed
  ) {
    auto rows = std::make_shared<std::vector<PermissionResult>>(*bulk->results);
    for (std::size_t i = 0; i < rows->size(); ++i) {
      auto& row = (*rows)[i];
      if (bulk->targets[i] && (!Action::keepSkipped || row.outcome != O::Skipped)) {
        row.outcome = O::Ready;
        row.error.reset();
        row.message = action.readyMessage(row);
      }
    }
    Reply reply{.bulk = bulk, .results = rows};
    try {
      // Validate shared input before any item changes, and resolve accounts just once.
      action.prepare(*bulk);
    } catch (const std::exception&) {
      const auto error = currentFileError();
      for (auto& row : *rows) {
        if (row.outcome == O::Ready) {
          row.message = "Not attempted: " + error.message;
        }
      }
      reply.report = "No changes attempted: " + error.message + "\n" + permissionReport(*rows);
      reply.error = error;
      return reply;
    }
    for (std::size_t i = 0; i < rows->size(); ++i) {
      auto& row = (*rows)[i];
      const auto& target = bulk->targets[i];
      if (row.outcome == O::Skipped) {
        completed.fetch_add(1);
        continue;
      }
      if (token.cancelled()) {
        if (target) {
          row.outcome = O::Cancelled;
          row.message = "Not attempted; cancelled.";
        }
        continue;
      }
      if (!target) {
        row.outcome = O::Failed;
        completed.fetch_add(1);
        continue;
      }
      reply.applied = action.apply(row, target) || reply.applied;
      completed.fetch_add(1);
    }
    for (std::size_t i = 0; i < rows->size(); ++i) {
      if (const auto& target = bulk->targets[i]) {
        action.settle((*rows)[i], target);
      }
    }
    bulk->results = rows;
    reply.finished = true;
    constexpr std::array labels{"ready", "changed", "unchanged", "failed", "partially changed", "cancelled", "skipped"};
    for (std::size_t i = 1; i < labels.size(); ++i) {
      const auto count = std::ranges::count(*rows, static_cast<O>(i), &PermissionResult::outcome);
      if (count != 0) {
        if (!reply.message.empty()) {
          reply.message += " · ";
        }
        reply.message += std::format("{} {}", count, labels[i]);
      }
    }
    reply.itemsFailed = std::ranges::any_of(*rows, [](const auto& row) {
      return row.outcome == O::Failed || row.outcome == O::Partial;
    });
    reply.report = permissionReport(*rows);
    return reply;
  }

  class ModeAction {
  public:
    // Recursive plans carry Skipped rows for links, mounts and repeated folders.
    static constexpr bool keepSkipped = true;

    ModeAction(
        PermissionEdit edit, std::optional<RecursivePermissionEdit> recursive, const CancelToken& token,
        std::atomic<std::size_t>& completed
    )
        : m_edit(std::move(edit)), m_recursive(std::move(recursive)), m_token(token), m_completed(completed) {}

    static std::string readyMessage(const PermissionResult& row) { return row.info ? row.info->reason : ""; }

    void prepare(const PermissionBatch& bulk) {
      checkModeBits(m_edit.mask, m_edit.mode);
      resolveAccounts(m_edit, [&bulk](gid_t group) {
        return std::ranges::any_of(bulk.targets, [group](const auto& target) {
          return target && target->baseline.st_gid != group;
        });
      });
    }

    bool apply(PermissionResult& row, const std::shared_ptr<PermissionTarget>& target) {
      bool applied = false;
      try {
        const auto key = std::pair{target->baseline.st_dev, target->baseline.st_ino};
        bool changedViaAlias = false;
        if (const auto found = m_aliases.find(key); found != m_aliases.end()
            && target->current()
            && (found->second.changed || samePermissions(target->baseline, found->second.value))
            && samePermissions(statFd(target->fd.get()), found->second.value)
            && (!target->aclBaseline
                || !target->aclBaseline->supported
                || (found->second.acl
                    && sameAcl(inspectAcl(target->fd.get()), *found->second.acl)
                    && (found->second.changed || sameAcl(*target->aclBaseline, *found->second.acl))))) {
          changedViaAlias = !samePermissions(target->baseline, found->second.value);
          target->baseline = found->second.value;
          if (target->aclBaseline && found->second.acl) {
            target->aclBaseline = found->second.acl;
            target->loadAcl(false);
          }
        }
        auto itemEdit = m_edit;
        if (m_recursive && S_ISDIR(target->baseline.st_mode)) {
          itemEdit.mode = m_recursive->directoryMode;
          itemEdit.mask = m_recursive->directoryMask;
        }
        auto item = applyPermissionEdit(target, itemEdit, m_token, !m_recursive.has_value());
        row.info = std::move(item.info);
        row.error = std::move(item.error);
        row.message = std::move(item.message);
        const bool changed = item.applied || changedViaAlias;
        row.outcome = row.error              ? (changed ? O::Partial : O::Failed)
            : changed && !complete(row.info) ? O::Partial
            : changed                        ? O::Changed
            : m_token.cancelled()            ? O::Cancelled
                                             : O::Unchanged;
        if (row.outcome == O::Partial && !row.error) {
          row.message = "Cancelled after applying some requested changes.";
        }
        if (!row.error && changedViaAlias && !item.applied) {
          row.message = "Updated through another selected hard link.";
        }
        applied = item.applied;
        if (!row.error || item.applied) {
          const auto previous = m_aliases.find(key);
          const bool aliasChanged = item.applied || (previous != m_aliases.end() && previous->second.changed);
          m_aliases.insert_or_assign(key, OwnChange{statFd(target->fd.get()), target->aclBaseline, aliasChanged});
        }
      } catch (const std::exception&) {
        row.outcome = O::Failed;
        row.error = currentFileError();
        row.message.clear();
        row.info.reset();
      }
      return applied;
    }

    void settle(PermissionResult& row, const std::shared_ptr<PermissionTarget>& target) {
      if (row.outcome == O::Cancelled && row.info) {
        settleCancelled(row, *target);
      } else if (row.outcome == O::Skipped) {
        try {
          row.info = target->inspect(false);
          if (!samePermissions(statFd(target->fd.get()), target->baseline)) {
            row.message += " Permissions changed through another name.";
          }
        } catch (const std::exception& error) {
          row.info.reset();
          row.message += std::string(" ") + error.what();
        }
      }
    }

  private:
    struct OwnChange {
      struct stat value;
      std::optional<AclInfo> acl;
      bool changed;
    };

    bool complete(const std::optional<PermissionInfo>& info) const {
      const bool folder = m_recursive && info && info->directory;
      const auto mode = folder ? m_recursive->directoryMode : m_edit.mode;
      const auto mask = folder ? m_recursive->directoryMask : m_edit.mask;
      return info
          && (!m_edit.owner || std::to_string(info->owner) == *m_edit.owner)
          && (!m_edit.group || std::to_string(info->group) == *m_edit.group)
          && (!mode || ((info->mode ^ *mode) & mask) == 0);
    }

    // Cancellation can leave unvisited names referring to an inode changed through
    // another selected hard link. Read the held inode without further account lookups.
    void settleCancelled(PermissionResult& row, PermissionTarget& target) {
      try {
        const auto value = statFd(target.fd.get());
        const auto own = m_aliases.find({value.st_dev, value.st_ino});
        if (own != m_aliases.end()
            && own->second.changed
            && samePermissions(value, own->second.value)
            && !samePermissions(value, target.baseline)) {
          row.outcome = O::Changed;
          row.message = "Changed through another selected hard link before cancellation.";
          m_completed.fetch_add(1);
        }
        auto& info = *row.info;
        if (info.owner != value.st_uid) {
          info.ownerName.clear();
        }
        if (info.group != value.st_gid) {
          info.groupName.clear();
        }
        info.owner = value.st_uid;
        info.group = value.st_gid;
        info.mode = value.st_mode & 07777;
        info.current = target.current();
        info.canMode = info.canOwner = info.canGroup = false;
        if (target.aclBaseline) {
          target.loadAcl(false);
          info.acl = target.aclView;
        }
        if (row.outcome == O::Changed && !complete(row.info)) {
          row.outcome = O::Partial;
          row.message = "Partially changed through another selected hard link before cancellation.";
        }
      } catch (const std::exception& error) {
        row.info.reset();
        row.message += std::string(" ") + error.what();
      }
    }

    PermissionEdit m_edit;
    std::optional<RecursivePermissionEdit> m_recursive;
    const CancelToken& m_token;
    std::atomic<std::size_t>& m_completed;
    std::map<std::pair<dev_t, ino_t>, OwnChange> m_aliases;
  };

  class AclAction {
  public:
    static constexpr bool keepSkipped = false;

    AclAction(AclPatchEdit edit, const CancelToken& token, std::atomic<std::size_t>& completed)
        : m_edit(std::move(edit)), m_token(token), m_completed(completed) {}

    static std::string_view readyMessage(const PermissionResult&) { return {}; }

    void prepare(const PermissionBatch&) { m_edit = normalizeAclPatch(std::move(m_edit), aclAccountId); }

    bool apply(PermissionResult& row, const std::shared_ptr<PermissionTarget>& target) {
      bool applied = false;
      try {
        const auto key = std::pair{target->baseline.st_dev, target->baseline.st_ino};
        const auto initial = target->baseline;
        const auto initialAcl = target->aclBaseline;
        bool changedViaAlias = false;
        if (const auto found = m_aliases.find(key); found != m_aliases.end()
            && target->aclBaseline
            && target->current()
            && samePermissions(statFd(target->fd.get()), found->second.value)
            && sameAcl(inspectAcl(target->fd.get()), found->second.acl)
            && ((samePermissions(target->baseline, found->second.value)
                 && sameAcl(*target->aclBaseline, found->second.acl))
                || (found->second.changed
                    && samePermissions(target->baseline, found->second.initial)
                    && sameAcl(*target->aclBaseline, found->second.initialAcl)))) {
          changedViaAlias = !samePermissions(target->baseline, found->second.value)
              || !sameAcl(*target->aclBaseline, found->second.acl);
          target->baseline = found->second.value;
          target->aclBaseline = found->second.acl;
          target->loadAcl(false);
        }
        target->validate();
        if (!target->aclView || !target->aclView->editable) {
          throwFileError(
              FileErrorKind::Unavailable, target->aclView ? target->aclView->message : "ACL editing is unavailable."
          );
        }
        const auto& actual = *target->aclBaseline;
        const bool skipDefaults = skipsDefaults(actual, m_edit);
        // Build and validate both desired tables before making either change.
        auto desired = patchAcl(actual, m_edit);
        auto item = applyAclEdit(target, desired, m_token);
        row.info = std::move(item.info);
        row.error = std::move(item.error);
        row.message = std::move(item.message);
        const bool changed = item.applied || changedViaAlias;
        row.outcome = row.error ? (changed ? O::Partial : O::Failed)
            : changed && (!row.info || !row.info->acl || !satisfiesAcl(*row.info->acl, desired)) ? O::Partial
            : changed                                                                            ? O::Changed
            : m_token.cancelled()                                                                ? O::Cancelled
            : !changesAccess(m_edit) && skipDefaults                                             ? O::Skipped
                                                                                                 : O::Unchanged;
        if (!row.error && changedViaAlias && !item.applied) {
          row.message = "Updated through another selected hard link.";
        }
        if (skipDefaults && m_edit.defaultPolicy != AclDefaultPolicy::Keep) {
          appendNote(
              row.message, actual.directory ? "Default ACL absent; preserved." : "Default ACLs apply only to folders."
          );
        }
        applied = item.applied;
        if ((!row.error || item.applied) && target->aclBaseline && target->aclBaseline->supported) {
          const auto previous = m_aliases.find(key);
          m_aliases.insert_or_assign(
              key,
              OwnChange{
                  target->baseline, *target->aclBaseline,
                  previous == m_aliases.end() ? initial : previous->second.initial,
                  previous == m_aliases.end() ? *initialAcl : previous->second.initialAcl,
                  item.applied || (previous != m_aliases.end() && previous->second.changed)
              }
          );
        }
      } catch (const std::exception&) {
        row.outcome = O::Failed;
        row.error = currentFileError();
        row.message.clear();
        target->loadAcl(false);
        row.info = target->inspect(false);
      }
      return applied;
    }

    // Unvisited aliases can already have effects. Report the held inode's actual
    // tables without adopting an external writer's state as a new baseline.
    void settle(PermissionResult& row, const std::shared_ptr<PermissionTarget>& target) {
      if (row.outcome != O::Cancelled) {
        return;
      }
      const auto value = statFd(target->fd.get());
      const auto actual = inspectAcl(target->fd.get());
      const auto own = m_aliases.find({value.st_dev, value.st_ino});
      if (own != m_aliases.end()
          && own->second.changed
          && target->aclBaseline
          && samePermissions(target->baseline, own->second.initial)
          && sameAcl(*target->aclBaseline, own->second.initialAcl)
          && samePermissions(value, own->second.value)
          && sameAcl(actual, own->second.acl)
          && (!samePermissions(value, target->baseline) || !sameAcl(actual, *target->aclBaseline))) {
        const auto desired = patchAcl(*target->aclBaseline, m_edit);
        row.outcome = satisfiesAcl(actual, desired) ? O::Changed : O::Partial;
        row.message = "Changed through another selected hard link before cancellation.";
        m_completed.fetch_add(1);
      }
      target->loadAcl(false);
      row.info = target->inspect(false);
      row.info->canMode = row.info->canOwner = row.info->canGroup = false;
    }

  private:
    struct OwnChange {
      struct stat value;
      AclInfo acl;
      struct stat initial;
      AclInfo initialAcl;
      bool changed;
    };

    AclPatchEdit m_edit;
    const CancelToken& m_token;
    std::atomic<std::size_t>& m_completed;
    std::map<std::pair<dev_t, ino_t>, OwnChange> m_aliases;
  };
} // namespace

luciole::PermissionReply luciole::applyBulkPermissionEdit(
    const std::shared_ptr<PermissionBatch>& bulk, PermissionEdit edit, const CancelToken& token,
    std::atomic<std::size_t>& completed
) {
  ModeAction action(std::move(edit), std::nullopt, token, completed);
  return runBulk(bulk, action, token, completed);
}

luciole::PermissionReply luciole::applyBulkAclPatch(
    const std::shared_ptr<PermissionBatch>& bulk, AclPatchEdit edit, const CancelToken& token,
    std::atomic<std::size_t>& completed
) {
  AclAction action(std::move(edit), token, completed);
  return runBulk(bulk, action, token, completed);
}

luciole::PermissionReply luciole::applyRecursivePermissionEdit(
    const std::shared_ptr<PermissionBatch>& roots, RecursivePermissionEdit edit, const CancelToken& token,
    std::atomic<std::size_t>& completed, std::atomic<std::size_t>& total, std::atomic<bool>& collecting
) {
  auto plan = std::make_shared<PermissionBatch>();
  plan->results = std::make_shared<std::vector<PermissionResult>>();
  auto initial = std::make_shared<std::vector<PermissionResult>>(*roots->results);
  for (std::size_t i = 0; i < initial->size(); ++i) {
    if (roots->targets[i]) {
      (*initial)[i].outcome = O::Ready;
      (*initial)[i].error.reset();
      (*initial)[i].message.clear();
    }
  }
  Reply reply{.bulk = roots, .results = initial};
  try {
    checkModeBits(edit.files.mask, edit.files.mode);
    checkModeBits(edit.directoryMask, edit.directoryMode);
    // Descendants are unknown until collection, so any group change needs membership.
    resolveAccounts(edit.files, [](gid_t) { return true; });
  } catch (const std::exception&) {
    const auto error = currentFileError();
    for (auto& row : *initial) {
      if (row.outcome == O::Ready) {
        row.message = "Not attempted: " + error.message;
      }
    }
    reply.report = "No changes attempted: " + error.message + "\n" + permissionReport(*initial);
    reply.error = error;
    collecting.store(false);
    return reply;
  }
  try {
    collectPermissionTargets(*roots, *plan, token, completed);
    if (!token.cancelled()) {
      total.store(plan->results->size());
      completed.store(0);
      collecting.store(false);
      ModeAction action(edit.files, edit, token, completed);
      reply = runBulk(plan, action, token, completed);
      reply.bulk = roots; // Release collected descriptors when the worker returns.
      reply.finished = true;
      return reply;
    }
    reply.message = "Cancelled while collecting targets. No changes attempted; total unknown.";
  } catch (const std::exception&) {
    reply.error = withPrefix("No changes attempted: ", currentFileError());
  }
  collecting.store(false);
  reply.finished = true;
  reply.results = plan->results;
  for (auto& row : *reply.results) {
    if (row.outcome == O::Ready) {
      row.outcome = O::Cancelled;
      row.message = "Not attempted; collection did not complete.";
    }
  }
  reply.report = (reply.error ? reply.error->message : reply.message) + "\n" + permissionReport(*reply.results);
  return reply;
}
