#pragma once

#include "operations/copy_history_tree.h"
#include "operations/move_receipt.h"

struct luciole::MoveIdentity {
  Fd from, to, item;
  std::shared_ptr<HistoryRevisions> revisions;
  std::shared_ptr<const MoveHistoryRoot> root;
};

struct luciole::MoveTrees {
  std::shared_ptr<const transfer::Tree> before, after;
  CopyHistoryTree::ChangedFolders changed;
  bool complete = false;
};
