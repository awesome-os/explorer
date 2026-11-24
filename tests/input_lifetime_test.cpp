#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "test_check.h"

#include <memory>
#include <print>
#include <stdexcept>

namespace {
  void sharedArea() {
    Node root;
    root.setSize(100, 100);
    auto node = std::make_unique<InputArea>();
    node->setSize(50, 50);
    node->setFocusable(true);
    auto* area = node.get();
    root.addChild(std::move(node));
    InputDispatcher first, second;
    first.setSceneRoot(&root);
    second.setSceneRoot(&root);
    first.setFocus(area);
    second.setFocus(area);
    first.pointerEnter(10, 10, 1);
    second.pointerEnter(10, 10, 1);
    first.pointerButton(10, 10, BTN_LEFT, true, 1, 0, false);
    TEST_CHECK(first.pointerCaptured());
    root.removeChild(area).reset();
    TEST_CHECK(
        !first.focusedArea()
        && !second.focusedArea()
        && !first.hoveredArea()
        && !second.hoveredArea()
        && !first.pointerCaptured()
    );
  }
  void reparentedArea() {
    Node left, right;
    left.setSize(100, 100);
    right.setSize(100, 100);
    auto node = std::make_unique<InputArea>();
    node->setSize(50, 50);
    node->setFocusable(true);
    auto* area = node.get();
    left.addChild(std::move(node));
    auto previous = std::make_unique<InputDispatcher>();
    previous->setSceneRoot(&left);
    previous->setFocus(area);
    right.addChild(left.removeChild(area));
    InputDispatcher current;
    current.setSceneRoot(&right);
    current.setFocus(area);
    previous.reset();
    right.removeChild(area).reset();
    TEST_CHECK(!current.focusedArea());
  }
} // namespace
int main() {
  try {
    sharedArea();
    reparentedArea();
    std::println("input lifetime checks passed");
  } catch (const std::exception& error) {
    std::println(stderr, "input lifetime test: {}", error.what());
    return 1;
  }
}
