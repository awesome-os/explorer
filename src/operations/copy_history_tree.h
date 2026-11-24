#pragma once

#include "operations/history_revisions.h"
#include "operations/transfer_copy.h"

#include <set>
#include <span>
#include <string>
#include <utility>

namespace luciole {
  // Compose and reverse recorded child publications in bounded tree proofs.
  class CopyHistoryTree {
  public:
    using ChangedFolders = std::set<HistoryRevisions::Key>;
    explicit CopyHistoryTree(transfer::Tree tree, ChangedFolders changed = {})
        : m_tree(std::move(tree)), m_changedFolders(std::move(changed)) {}
    const transfer::Tree& tree() const { return m_tree; }
    const ChangedFolders& changedFolders() const { return m_changedFolders; }
    void replace(
        std::span<const std::string> path, const transfer::Tree& incoming, const transfer::Tree* previous,
        const HistoryRevisions& revisions, const CancelToken& token
    );
    void rewind(
        std::span<const std::string> path, const transfer::Tree& copied, const transfer::Tree* previous,
        const HistoryRevisions& revisions, const CancelToken& token
    );
    void matches(const transfer::Tree& previous, const HistoryRevisions& revisions, const CancelToken& token) const;
    transfer::Tree finish(
        int parent, const std::string& name, HistoryRevisions& revisions, const CancelToken& token,
        std::size_t* budget = nullptr
    );

  private:
    void compare(
        const transfer::Tree& expected, const transfer::Tree& actual, const HistoryRevisions& revisions,
        const CancelToken& token
    ) const;
    void metadata(const struct stat& expected, const struct stat& actual, const HistoryRevisions& revisions) const;
    void verify(
        transfer::Tree& tree, int parent, const std::string& name, HistoryRevisions& revisions,
        const CancelToken& token, std::size_t& remaining, unsigned depth
    );
    transfer::Tree m_tree;
    ChangedFolders m_changedFolders;
  };
} // namespace luciole
