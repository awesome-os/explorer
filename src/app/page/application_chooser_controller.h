#pragma once

#include "core/timer_manager.h"

#include <memory>
#include <string>
#include <vector>

namespace luciole {
  class FileIconCache;
  class FileLauncher;
  struct DirectoryEntry;
  struct ApplicationChoice;
  struct ApplicationList;
} // namespace luciole

class Button;
class Flex;
class Input;
class InputArea;
class Label;
class PageContext;
class ScrollView;
class Toggle;

// The Open With dialog: suggested/installed applications and explicit default association changes.
class ApplicationChooserController {
public:
  ApplicationChooserController(PageContext& page, luciole::FileLauncher& launcher, luciole::FileIconCache& icons);
  void show(const luciole::DirectoryEntry& entry);
  void show(const std::vector<luciole::DirectoryEntry>& entries);
  void revealFocus(InputArea* focused);

private:
  struct Session;
  bool current(const std::shared_ptr<Session>& session) const;
  void load();
  void populate(const std::shared_ptr<Session>& session, luciole::ApplicationList apps);
  void choose(const std::shared_ptr<Session>& session, const luciole::ApplicationChoice& app);
  void enableChoices(bool enabled);
  void showError(const std::string& message);

  PageContext& m_page;
  luciole::FileLauncher& m_launcher;
  luciole::FileIconCache& m_icons;
  std::shared_ptr<Session> m_session;
  Timer m_searchDelay;
  Flex* m_container = nullptr;
  ScrollView* m_scroll = nullptr;
  Input* m_search = nullptr;
  Button* m_other = nullptr;
  Toggle* m_default = nullptr;
  Label* m_error = nullptr;
  std::vector<Button*> m_choices;
  std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
};
