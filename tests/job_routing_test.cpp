#include "browser/job_routing.h"
#include "test_check.h"

#include <print>
#include <stdexcept>

namespace {
  luciole::TabId active(const luciole::TabWorkspace& workspace, luciole::WindowId window) {
    return workspace.windows().at(window).active;
  }

  void staysWithItsPage() {
    luciole::TabWorkspace workspace;
    const auto first = workspace.addWindow();
    const auto second = workspace.addWindow();
    const auto origin = workspace.addTab(first);
    const auto other = workspace.addTab(first);
    (void)workspace.addTab(second);
    luciole::JobRouting routing;
    routing.add(1, origin, workspace);
    TEST_CHECK(routing.owner(1) == origin);
    TEST_CHECK(workspace.select(first, other));
    TEST_CHECK(routing.update(workspace).empty() && routing.owner(1) == origin);
    TEST_CHECK(workspace.move(origin, second, 0));
    TEST_CHECK(routing.update(workspace).empty() && routing.owner(1) == origin);
    // After the move the job follows the new window: closing the page hands it to that window's active page.
    TEST_CHECK(workspace.close(origin));
    const auto moves = routing.update(workspace);
    TEST_CHECK(moves.size() == 1 && moves.front().from == origin && moves.front().to == active(workspace, second));
    TEST_CHECK(routing.owner(1) == active(workspace, second));
    const auto start = routing.origin(1);
    TEST_CHECK(start && start->window == first && start->page == origin);
    TEST_CHECK(!routing.origin(2));
  }

  void movesToActiveTabOfSameWindow() {
    luciole::TabWorkspace workspace;
    const auto first = workspace.addWindow();
    const auto second = workspace.addWindow();
    const auto origin = workspace.addTab(first);
    const auto sibling = workspace.addTab(first);
    const auto elsewhere = workspace.addTab(second);
    luciole::JobRouting routing;
    routing.add(1, origin, workspace);
    routing.add(2, origin, workspace);
    routing.add(3, elsewhere, workspace);
    TEST_CHECK(workspace.close(origin));
    // The preferred window matters only when the job's own window is gone.
    const auto moves = routing.update(workspace, second);
    TEST_CHECK(moves.size() == 2 && moves[0].job == 1 && moves[1].job == 2);
    TEST_CHECK(routing.owner(1) == sibling && routing.owner(2) == sibling && routing.owner(3) == elsewhere);
  }

  void splitPaneHandsOverToTheOtherPane() {
    luciole::TabWorkspace workspace;
    const auto window = workspace.addWindow();
    const auto primary = workspace.addTab(window);
    const auto pane = workspace.addPane(window);
    TEST_CHECK(pane != 0 && active(workspace, window) == pane);
    luciole::JobRouting routing;
    routing.add(1, pane, workspace);
    TEST_CHECK(workspace.closePane(window, pane));
    TEST_CHECK(routing.update(workspace).size() == 1 && routing.owner(1) == primary);
  }

  void windowGoneUsesPreferredThenAnyWindow() {
    luciole::TabWorkspace workspace;
    const auto first = workspace.addWindow();
    const auto second = workspace.addWindow();
    const auto third = workspace.addWindow();
    const auto origin = workspace.addTab(first);
    const auto inSecond = workspace.addTab(second);
    const auto inThird = workspace.addTab(third);
    luciole::JobRouting routing;
    routing.add(1, origin, workspace);
    routing.add(2, inSecond, workspace);
    TEST_CHECK(workspace.close(origin) && workspace.removeWindow(first));
    TEST_CHECK(routing.update(workspace, third).size() == 1 && routing.owner(1) == inThird);
    TEST_CHECK(workspace.close(inThird) && workspace.removeWindow(third));
    TEST_CHECK(routing.update(workspace, third).size() == 1 && routing.owner(1) == inSecond);
    TEST_CHECK(routing.owner(2) == inSecond);
  }

  void waitsForAWindow() {
    luciole::TabWorkspace workspace;
    const auto window = workspace.addWindow();
    const auto origin = workspace.addTab(window);
    luciole::JobRouting routing;
    routing.add(1, origin, workspace);
    TEST_CHECK(workspace.close(origin) && workspace.removeWindow(window));
    auto moves = routing.update(workspace);
    TEST_CHECK(moves.size() == 1 && moves.front().from == origin && moves.front().to == 0);
    TEST_CHECK(routing.owner(1) == 0);
    TEST_CHECK(routing.update(workspace).empty());
    const auto next = workspace.addWindow();
    const auto page = workspace.addTab(next);
    moves = routing.update(workspace);
    TEST_CHECK(moves.size() == 1 && moves.front().from == 0 && moves.front().to == page);
    TEST_CHECK(routing.owner(1) == page);
    routing.remove(1);
    TEST_CHECK(routing.owner(1) == 0 && routing.update(workspace).empty());
  }

  void pageBoundJobsStay() {
    luciole::TabWorkspace workspace;
    const auto window = workspace.addWindow();
    const auto origin = workspace.addTab(window);
    const auto sibling = workspace.addTab(window);
    luciole::JobRouting routing;
    routing.add(1, origin, workspace, false);
    routing.add(2, origin, workspace, false);
    TEST_CHECK(!routing.following(1));
    routing.follow(2);
    TEST_CHECK(routing.following(2));
    TEST_CHECK(workspace.close(origin));
    const auto moves = routing.update(workspace);
    TEST_CHECK(moves.size() == 2);
    TEST_CHECK(moves[0].job == 1 && moves[0].from == origin && moves[0].to == 0);
    TEST_CHECK(moves[1].job == 2 && moves[1].to == sibling);
    TEST_CHECK(routing.owner(1) == 0 && routing.owner(2) == sibling);
    // An ownerless page-bound job is not handed to a page later.
    TEST_CHECK(routing.update(workspace).empty() && routing.owner(1) == 0);
  }
} // namespace

int main() {
  try {
    staysWithItsPage();
    movesToActiveTabOfSameWindow();
    splitPaneHandsOverToTheOtherPane();
    windowGoneUsesPreferredThenAnyWindow();
    waitsForAWindow();
    pageBoundJobsStay();
    std::println("job routing checks passed");
  } catch (const std::exception& error) {
    std::println(stderr, "job routing test: {}", error.what());
    return 1;
  }
}
