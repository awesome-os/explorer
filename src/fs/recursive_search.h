#pragma once

#include "fs/directory_session.h"

namespace luciole {
  // Default-context GIO traversal, one directory and one 64-entry batch in flight.
  class RecursiveSearch {
  public:
    RecursiveSearch();
    ~RecursiveSearch();
    RecursiveSearch(const RecursiveSearch&) = delete;
    RecursiveSearch& operator=(const RecursiveSearch&) = delete;

    void start(const Location& root, const std::string& query, bool showHidden, bool showBackups = true);
    void cancel();
    void setChangedCallback(std::function<void()> callback);
    const DirectorySnapshot& snapshot() const;
    std::size_t skippedFolders() const;

  private:
    struct State;
    std::shared_ptr<State> m_state;
  };
} // namespace luciole
