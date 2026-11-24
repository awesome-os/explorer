#include "operations/copy_history_tree.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace {
  void check(const luciole::CancelToken& token) {
    if (token.cancelled()) {
      luciole::throwFileError(luciole::FileErrorKind::Cancelled, "Transfer history capture cancelled.");
    }
  }
  [[noreturn]] void changed() {
    luciole::throwFileError(luciole::FileErrorKind::Changed, "Overlapping transfer results changed during the job.");
  }
} // namespace

void luciole::CopyHistoryTree::metadata(
    const struct stat& expected, const struct stat& actual, const HistoryRevisions& revisions
) const {
  const bool changedFolder = m_changedFolders.contains({expected.st_dev, expected.st_ino});
  auto value = revisions.expected(expected, changedFolder);
  if (S_ISDIR(value.st_mode) && m_changedFolders.contains({value.st_dev, value.st_ino})) {
    // Exact children are checked separately; only their parent namespace bookkeeping may differ.
    value.st_size = actual.st_size;
    value.st_nlink = actual.st_nlink;
    value.st_mtim = actual.st_mtim;
    value.st_ctim = actual.st_ctim;
  }
  if (!sameFileRevision(value, actual)) {
    changed();
  }
}

void luciole::CopyHistoryTree::compare(
    const transfer::Tree& expected, const transfer::Tree& actual, const HistoryRevisions& revisions,
    const CancelToken& token
) const {
  check(token);
  metadata(
      expected.metadata,
      revisions.expected(actual.metadata, m_changedFolders.contains({actual.metadata.st_dev, actual.metadata.st_ino})),
      revisions
  );
  if (expected.children.size() != actual.children.size()) {
    changed();
  }
  std::unordered_map<std::string_view, const transfer::Tree*> children;
  for (const auto& child : actual.children) {
    children.emplace(child.name, &child);
  }
  for (const auto& child : expected.children) {
    const auto found = children.find(child.name);
    if (found == children.end()) {
      changed();
    }
    compare(child, *found->second, revisions, token);
  }
}

void luciole::CopyHistoryTree::replace(
    std::span<const std::string> path, const transfer::Tree& incoming, const transfer::Tree* previous,
    const HistoryRevisions& revisions, const CancelToken& token
) {
  if (path.empty() || path.size() > 64) {
    changed();
  }
  auto* node = &m_tree;
  for (std::size_t n = 0; n < path.size(); ++n) {
    check(token);
    if (!S_ISDIR(node->metadata.st_mode)) {
      changed();
    }
    m_changedFolders.emplace(node->metadata.st_dev, node->metadata.st_ino);
    const auto found = std::ranges::find(node->children, path[n], &transfer::Tree::name);
    if (n + 1 == path.size()) {
      if ((found == node->children.end()) != (previous == nullptr)) {
        changed();
      }
      if (previous) {
        compare(*found, *previous, revisions, token);
      }
      auto next = incoming;
      next.name = path[n];
      if (found == node->children.end()) {
        node->children.push_back(std::move(next));
      } else {
        *found = std::move(next);
      }
    } else {
      if (found == node->children.end()) {
        changed();
      }
      node = &*found;
    }
  }
}

void luciole::CopyHistoryTree::rewind(
    std::span<const std::string> path, const transfer::Tree& copied, const transfer::Tree* previous,
    const HistoryRevisions& revisions, const CancelToken& token
) {
  if (path.empty() || path.size() > 64) {
    changed();
  }
  auto* node = &m_tree;
  for (std::size_t n = 0; n < path.size(); ++n) {
    check(token);
    if (!S_ISDIR(node->metadata.st_mode)) {
      changed();
    }
    m_changedFolders.emplace(node->metadata.st_dev, node->metadata.st_ino);
    const auto found = std::ranges::find(node->children, path[n], &transfer::Tree::name);
    if (found == node->children.end()) {
      changed();
    }
    if (n + 1 == path.size()) {
      compare(*found, copied, revisions, token);
      if (previous) {
        auto restored = *previous;
        restored.name = path[n];
        *found = std::move(restored);
      } else {
        node->children.erase(found);
      }
    } else {
      node = &*found;
    }
  }
}

void luciole::CopyHistoryTree::matches(
    const transfer::Tree& previous, const HistoryRevisions& revisions, const CancelToken& token
) const {
  compare(m_tree, previous, revisions, token);
}

void luciole::CopyHistoryTree::verify(
    transfer::Tree& tree, int parent, const std::string& name, HistoryRevisions& revisions, const CancelToken& token,
    std::size_t& remaining, unsigned depth
) {
  check(token);
  if (!remaining || depth > 64) {
    throwFileError(FileErrorKind::Unsupported, "Combined transfer history exceeds its tree limit.");
  }
  --remaining;
  const auto current = transfer::info(parent, name);
  metadata(tree.metadata, current, revisions);
  if (S_ISDIR(current.st_mode)) {
    const auto directory = transfer::directory(parent, name);
    if (!sameFileRevision(current, statFd(directory.get()))
        || transfer::children(directory.get(), tree.children.size() + 1).size() != tree.children.size()) {
      changed();
    }
    for (auto& child : tree.children) {
      verify(child, directory.get(), child.name, revisions, token, remaining, depth + 1);
    }
    if (!sameFileRevision(current, statFd(directory.get()))) {
      changed();
    }
    if (m_changedFolders.contains({current.st_dev, current.st_ino})) {
      revisions.directoryChanged(tree.metadata, current);
    }
  }
  tree.metadata = current;
}

luciole::transfer::Tree luciole::CopyHistoryTree::finish(
    int parent, const std::string& name, HistoryRevisions& revisions, const CancelToken& token, std::size_t* budget
) {
  std::size_t remaining = 16384;
  verify(m_tree, parent, name, revisions, token, budget ? *budget : remaining, 0);
  return std::move(m_tree);
}
