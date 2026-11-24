#pragma once

#include "browser/action_registry.h"
#include "config/keybinds.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace luciole {
  bool shortcutMatches(BrowserAction action, std::uint32_t sym, std::uint32_t modifiers);
  // The first of `actions` whose shortcut matches the key.
  std::optional<BrowserAction>
  shortcutAction(std::initializer_list<BrowserAction> actions, std::uint32_t sym, std::uint32_t modifiers);
  // The action's configured chords joined with " / "; empty when it has no keybind.
  std::string actionShortcutLabel(BrowserAction action);
  std::string shortcutTooltip(std::string_view text, BrowserAction action);
  std::string shortcutTooltip(std::string_view text, KeybindCommand command);
} // namespace luciole
