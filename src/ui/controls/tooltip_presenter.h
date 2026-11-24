#pragma once

#include "core/timer_manager.h"
#include "render/scene/tooltip_content.h"
#include "ui/browser/file_tooltip.h"
#include "ui/controls/flex.h"

#include <functional>
#include <string>

class InputArea;
class Label;

// Window-local adaptation of Noctalia's shell/tooltip/tooltip_manager.
// The overlay borrows no control callbacks or lifetime-sensitive tooltip providers.
class TooltipPresenter final : public Flex {
public:
  explicit TooltipPresenter(std::function<void()> requestUpdate);

  void resume() noexcept { m_armed = true; }
  void dismiss();
  void sync(
      Renderer& renderer, Node& root, float pointerX, float pointerY, LayoutSize bounds,
      std::optional<luciole::FileTooltipTarget> file = std::nullopt
  );

private:
  std::function<void()> m_requestUpdate;
  Timer m_delay;
  Timer m_refresh;
  luciole::FileTooltipData m_fileData;
  Label* m_label = nullptr;
  Flex* m_table = nullptr;
  struct Row {
    Flex* container;
    Label* key;
    Label* value;
  };
  std::vector<Row> m_rows;
  const InputArea* m_identity = nullptr; // Compared only; resolved afresh from the live tree each frame.
  std::optional<luciole::FileTooltipTarget> m_file;
  TooltipContent m_controlContent;
  LayoutRect m_anchor{};
  TooltipPlacement m_placement = TooltipPlacement::Default;
  bool m_ready = false;
  bool m_armed = false;
  bool m_refreshDue = false;
  std::chrono::milliseconds m_interval{};
  void populate(Renderer& renderer, const TooltipContent& content, LayoutSize bounds);
  void place(Renderer& renderer, LayoutSize bounds);
};
