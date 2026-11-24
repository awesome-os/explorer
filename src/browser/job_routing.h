#pragma once

#include "browser/tab_workspace.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace luciole {
  using JobId = std::uint64_t;

  // Which page shows each file job. A job starts on its origin page and stays with the page that owns it while that
  // page belongs to a window. Once the page leaves its window, a following job moves to the active page of the same
  // window, or to the active page of another open window when that window is gone; without any window it has no
  // owner until one opens. A page-bound job (a dialog's preparation) never moves: it loses its owner instead.
  class JobRouting {
  public:
    struct Place {
      WindowId window = 0;
      TabId page = 0;
    };
    struct Move {
      JobId job = 0;
      TabId from = 0, to = 0;
    };

    // Starts routing `job` to `page`, which should belong to a window of `workspace`.
    void add(JobId job, TabId page, const TabWorkspace& workspace, bool follow = true);
    void remove(JobId job);
    // Lets a page-bound job follow its page's window from now on.
    void follow(JobId job);
    bool following(JobId job) const;
    // The page that shows `job`; 0 while it has no owner.
    TabId owner(JobId job) const;
    // The window and page that started `job`.
    std::optional<Place> origin(JobId job) const;
    // Moves every job whose page left its window; `preferred` is the window to try first when the job's own window
    // is gone. Returns the owner changes, oldest job first; `to` is 0 when the job is page-bound or no window
    // remains.
    std::vector<Move> update(const TabWorkspace& workspace, std::optional<WindowId> preferred = std::nullopt);

  private:
    struct Route {
      Place origin;
      Place owner;
      bool follow = true;
    };
    std::map<JobId, Route> m_routes;
  };
} // namespace luciole
