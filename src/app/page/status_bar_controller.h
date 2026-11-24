#pragma once

#include "core/timer_manager.h"
#include "render/scene/node.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace luciole {
  class PinService;
} // namespace luciole

class AnimationManager;
class FileKeyboardController;
class Flex;
class Glyph;
class Label;
class PageContext;
class Renderer;
class SettingsController;

// The status tab docked to the bottom of the page body and the empty-folder notice: the last
// action message, search and selection summaries, free space, and the notice shown in place of an empty file list.
// The tab shows only when there is something to report unless the Always show status bar setting keeps the idle
// folder count visible. It fades and slides in and out.
class StatusBarController {
public:
  StatusBarController(
      PageContext& page, AnimationManager& animations, luciole::PinService& pins, SettingsController& settings,
      FileKeyboardController& keys
  );
  ~StatusBarController();

  // The last action message; shown alongside search/selection information until the next action or a few seconds.
  void setMessage(std::string message);
  void clearMessage();

  // Scene: the status tab (an overlay child of the page body) and the notice inside the file pane.
  std::unique_ptr<Flex> build();
  std::unique_ptr<Flex> buildNotice();

  // BrowserPage::syncBrowser(): status text, free space and the notice for an empty list.
  void sync(bool miller);
  // Layout hook in BrowserPage::prepareFrame(): free space shows only in a pane at least that wide.
  void layout(float paneWidth);
  // Docks the tab to the bottom edge of the page body (`width` x `height`, body coordinates) at the trailing side.
  void arrange(Renderer& renderer, float width, float height);
  // True while the notice replaces the file list.
  bool noticeShown() const;
  // Scene rectangle of the status text, where the input method places its candidates.
  LayoutRect statusRect() const;

private:
  void reveal();

  PageContext& m_page;
  AnimationManager& m_animations;
  luciole::PinService& m_pins;
  SettingsController& m_settings;
  FileKeyboardController& m_keys;
  std::string m_actionError;
  Timer m_messageTimer;
  std::uint64_t m_statusContext = 0;
  bool m_contextLoaded = false;
  std::optional<std::uint64_t> m_freeBytes;
  Flex* m_pill = nullptr;
  Label* m_message = nullptr;
  Label* m_status = nullptr;
  Label* m_freeSpace = nullptr;
  bool m_freeSpaceAvailable = false;
  bool m_needed = false;
  float m_reveal = 0.0F;
  Glyph* m_noticeGlyph = nullptr;
  Label* m_notice = nullptr;
  Label* m_noticeDetail = nullptr;
  Flex* m_noticeArea = nullptr;
};
