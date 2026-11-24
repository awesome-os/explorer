#include "operations/copy_receipt.h"
#include "operations/history_revisions.h"
#include "operations/move_history_internal.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <set>
#include <unistd.h>

namespace {
  void check(const luciole::CancelToken& token) {
    if (token.cancelled()) {
      luciole::throwFileError(luciole::FileErrorKind::Cancelled, "Move history validation cancelled.");
    }
  }
  std::optional<std::vector<std::string>> relative(const luciole::Location& root, luciole::Location child) {
    std::vector<std::string> path;
    for (unsigned depth = 0; depth <= 64; ++depth) {
      if (root == child) {
        std::ranges::reverse(path);
        return path;
      }
      const auto parent = child.parent();
      const auto name = child.basename();
      if (!parent || !name) {
        break;
      }
      path.push_back(*name);
      child = *parent;
    }
    return {};
  }
  void countTree(
      const luciole::transfer::Tree& tree, std::size_t& remaining, const luciole::CancelToken& token, unsigned depth = 0
  ) {
    check(token);
    if (!remaining || depth > 64) {
      luciole::throwFileError(luciole::FileErrorKind::Unsupported, "Move ancestor history exceeds its tree limit.");
    }
    --remaining;
    for (const auto& child : tree.children) {
      countTree(child, remaining, token, depth + 1);
    }
  }
  luciole::Fd directory(int item) {
    return luciole::checkedFd(
        ::open(("/proc/self/fd/" + std::to_string(item)).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)
    );
  }
} // namespace

bool luciole::MoveReceipt::needsSequence(
    const std::vector<std::shared_ptr<const MoveEffect>>& effects, const CancelToken& token
) {
  for (std::size_t i = 0; i < effects.size(); ++i) {
    check(token);
    const auto& effect = effects[i];
    if (effect->m_reversed || !effect->m_previous || !S_ISDIR(effect->m_previous->m_tree->metadata.st_mode)) {
      continue;
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (const auto path = relative(effect->m_destination, effects[j]->m_destination); path && !path->empty()) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::shared_ptr<const luciole::MoveEffect>> luciole::MoveReceipt::checkpoint(
    const std::vector<std::shared_ptr<const MoveEffect>>& effects, const Location& destination, std::size_t& remaining,
    const CancelToken& token, const std::vector<std::shared_ptr<const MoveFolder>>& folders
) {
  const bool affected = std::ranges::any_of(effects, [&](const auto& effect) {
    const auto path = relative(destination, effect->m_destination);
    return path && !path->empty();
  });
  if (!affected) {
    return effects;
  }
  MoveReceipt proof;
  proof.m_effects = effects;
  proof.m_folders = folders;
  auto result = effects;
  for (auto& effect : result) {
    const auto path = relative(destination, effect->m_destination);
    if (!path || effect->m_trees || !S_ISDIR(effect->m_revision.st_mode)) {
      continue;
    }
    const auto bindings = proof.sequenceParents(*effect, token);
    const auto retained = std::ranges::find_if(effects, [&](const auto& other) {
      return other->m_previous
          && sameIdentity(statFd(other->m_previous->item()), statFd(effect->m_identity->item.get()));
    });
    if (retained != effects.end()) {
      (*retained)->m_previous->validate(token, bindings);
      auto trees = std::make_shared<MoveTrees>();
      trees->before = trees->after = (*retained)->m_previous->m_tree;
      auto next = std::shared_ptr<MoveEffect>(new MoveEffect(*effect));
      next->m_trees = std::move(trees);
      effect = std::move(next);
    } else {
      effect = effect->captureContents(remaining, token, bindings);
    }
  }
  return compose(result, true, token);
}

std::vector<std::shared_ptr<const luciole::MoveEffect>> luciole::MoveReceipt::prepareSequence(
    const std::vector<std::shared_ptr<const MoveEffect>>& effects, const CancelToken& token
) {
  auto result = compose(effects, true, token);
  std::size_t remaining = 16384;
  for (std::size_t i = 0; i < result.size(); ++i) {
    check(token);
    const auto& effect = result[i];
    if (!effect->m_previous || !S_ISDIR(effect->m_previous->m_tree->metadata.st_mode)) {
      continue;
    }
    CopyHistoryTree draft(*effect->m_previous->m_tree);
    for (std::size_t j = i; j > 0; --j) {
      check(token);
      const auto& earlier = result[j - 1];
      if (relative(earlier->m_destination, effect->m_destination)) {
        break; // This version arrived with that earlier ancestor, not with the job's initial destination.
      }
      const auto path = relative(effect->m_destination, earlier->m_destination);
      if (!path || path->empty()) {
        continue;
      }
      const transfer::Tree leaf{.name = "item", .metadata = earlier->m_revision, .children = {}};
      if (S_ISDIR(earlier->m_revision.st_mode) && !earlier->m_trees) {
        throwFileError(FileErrorKind::Changed, "An earlier moved folder has no ancestor history proof.");
      }
      draft.rewind(
          *path, earlier->m_trees ? *earlier->m_trees->before : leaf,
          earlier->m_previous ? earlier->m_previous->m_tree.get() : nullptr, *effect->m_identity->revisions, token
      );
    }
    countTree(draft.tree(), remaining, token);
    auto trees = std::make_shared<MoveTrees>();
    trees->before = std::make_shared<const transfer::Tree>(draft.tree());
    trees->after = effect->m_previous->m_tree;
    trees->changed = draft.changedFolders();
    trees->complete = true;
    auto next = std::shared_ptr<MoveEffect>(new MoveEffect(*effect));
    next->m_previousTrees = std::move(trees);
    result[i] = std::move(next);
  }
  return result;
}

luciole::MoveParents luciole::MoveReceipt::sequenceParents(const MoveEffect& effect, const CancelToken& token) const {
  auto result = parents(token);
  const auto bind = [&](const Location& location, int held) {
    const auto logical = *location.parent();
    const auto tryRoot = [&](const Location& root, int item) {
      check(token);
      const auto path = relative(root, logical);
      if (!path) {
        return false;
      }
      try {
        auto actual = directory(item);
        for (const auto& name : *path) {
          check(token);
          actual = transfer::directory(actual.get(), name);
        }
        if (sameIdentity(statFd(actual.get()), statFd(held))) {
          result[logical.uri()] = held;
          return true;
        }
      } catch (const FileErrorException& error) {
        if (error.error().kind != FileErrorKind::Missing && error.error().kind != FileErrorKind::NotDirectory) {
          throw;
        }
      }
      return false;
    };
    for (const auto& other : m_effects) {
      if (S_ISDIR(other->m_revision.st_mode)
          && tryRoot(other->m_reversed ? other->m_source : other->m_destination, other->m_identity->item.get())) {
        return;
      }
      if (other->m_previous
          && S_ISDIR(other->m_previous->m_tree->metadata.st_mode)
          && tryRoot(other->m_previous->m_destination, other->m_previous->item())) {
        return;
      }
    }
  };
  bind(effect.m_source, effect.sourceParent());
  bind(effect.m_destination, effect.destinationParent());
  if (effect.m_previous) {
    bind(effect.m_previous->m_destination, effect.m_previous->parent());
  }
  return result;
}

std::shared_ptr<const luciole::CopyEffect> luciole::MoveReceipt::sequencePrevious(
    const MoveEffect& effect, bool before, const CancelToken& token, const MoveParents& bindings
) const {
  const auto& previous = effect.m_previous;
  if (!effect.m_previousTrees) {
    previous->validate(token, bindings);
    return previous;
  }
  const auto& trees = *effect.m_previousTrees;
  CopyHistoryTree draft(*(before ? trees.before : trees.after), trees.changed);
  auto fd = directory(previous->item());
  auto tree = std::make_shared<const transfer::Tree>(draft.finish(fd.get(), ".", *m_revisions, token));
  return previous->withTree(std::move(tree), token, bindings);
}

std::shared_ptr<const luciole::MoveEffect>
luciole::MoveReceipt::replayEffect(const std::shared_ptr<const MoveEffect>& effect, const CancelToken& token) const {
  if (!m_sequence || !effect->m_previous) {
    return effect;
  }
  auto next = std::shared_ptr<MoveEffect>(new MoveEffect(*effect));
  next->m_previous = sequencePrevious(*effect, false, token, {});
  return next;
}

void luciole::MoveReceipt::validateSequence(const CancelToken& token) const {
  validateFolders(parents(token), token);
  std::set<HistoryRevisions::Key> incoming;
  std::map<HistoryRevisions::Key, std::shared_ptr<const CopyEffect>> hidden;
  for (const auto& effect : m_effects) {
    const auto stamp = statFd(effect->m_identity->item.get());
    incoming.emplace(stamp.st_dev, stamp.st_ino);
    if (effect->m_previous && effect->m_previous->m_hidden) {
      const auto previous = statFd(effect->m_previous->item());
      hidden[{previous.st_dev, previous.st_ino}] = effect->m_previous;
    }
  }
  for (const auto& effect : m_effects) {
    check(token);
    const auto bindings = sequenceParents(*effect, token);
    const auto stamp = statFd(effect->m_identity->item.get());
    const auto retained = hidden.find({stamp.st_dev, stamp.st_ino});
    if (retained != hidden.end()) {
      retained->second->validate(token, bindings);
      if (effect->m_trees) {
        CopyHistoryTree proof(*effect->m_trees->after, effect->m_trees->changed);
        auto fd = directory(effect->m_identity->item.get());
        (void)proof.finish(fd.get(), ".", *m_revisions, token);
      } else {
        m_revisions->validate(effect->m_revision, stamp);
      }
    } else {
      auto standalone = *effect;
      standalone.m_previous.reset();
      standalone.validate(effect->m_revision, bindings, false, token);
    }
    if (effect->m_previous) {
      const auto previous = statFd(effect->m_previous->item());
      if (!incoming.contains({previous.st_dev, previous.st_ino})) {
        (void)sequencePrevious(*effect, m_undone, token, bindings);
      }
    }
    if (!effect->m_reversed || !effect->m_previous) {
      effect->requireVacant(bindings);
    }
  }
}
