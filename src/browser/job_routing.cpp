#include "browser/job_routing.h"

void luciole::JobRouting::add(JobId job, TabId page, const TabWorkspace& workspace, bool follow) {
  const Place place{.window = workspace.owner(page).value_or(0), .page = page};
  m_routes.insert_or_assign(job, Route{.origin = place, .owner = place, .follow = follow});
}

void luciole::JobRouting::remove(JobId job) { m_routes.erase(job); }

void luciole::JobRouting::follow(JobId job) {
  if (const auto found = m_routes.find(job); found != m_routes.end()) {
    found->second.follow = true;
  }
}

bool luciole::JobRouting::following(JobId job) const {
  const auto found = m_routes.find(job);
  return found != m_routes.end() && found->second.follow;
}

luciole::TabId luciole::JobRouting::owner(JobId job) const {
  const auto found = m_routes.find(job);
  return found == m_routes.end() ? 0 : found->second.owner.page;
}

std::optional<luciole::JobRouting::Place> luciole::JobRouting::origin(JobId job) const {
  const auto found = m_routes.find(job);
  if (found == m_routes.end()) {
    return std::nullopt;
  }
  return found->second.origin;
}

std::vector<luciole::JobRouting::Move>
luciole::JobRouting::update(const TabWorkspace& workspace, std::optional<WindowId> preferred) {
  const auto& windows = workspace.windows();
  const auto activeIn = [&windows](WindowId id) -> std::optional<Place> {
    const auto found = windows.find(id);
    if (found == windows.end() || !found->second.active) {
      return std::nullopt;
    }
    return Place{.window = id, .page = found->second.active};
  };
  std::optional<Place> fallback;
  for (const auto& [id, window] : windows) {
    if (window.active) {
      fallback = Place{.window = id, .page = window.active};
      break;
    }
  }
  std::vector<Move> moved;
  for (auto& [job, route] : m_routes) {
    if (route.owner.page) {
      if (const auto window = workspace.owner(route.owner.page)) {
        route.owner.window = *window;
        continue;
      }
    } else if (!route.follow) {
      continue;
    }
    std::optional<Place> next;
    if (route.follow) {
      next = activeIn(route.owner.window);
      if (!next && preferred) {
        next = activeIn(*preferred);
      }
      if (!next) {
        next = fallback;
      }
    }
    const auto target = next.value_or(Place{.window = route.owner.window, .page = 0});
    if (target.page != route.owner.page) {
      moved.push_back({.job = job, .from = route.owner.page, .to = target.page});
      route.owner = target;
    }
  }
  return moved;
}
