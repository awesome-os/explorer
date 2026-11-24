#pragma once

#include "operations/batch_rename_service.h"
#include "ui/controls/flex.h"

class Input;
class Label;
class Button;
class Stepper;
class Toggle;
class Segmented;
class VirtualGridView;
class ScrollView;
class TooltipPresenter;

class BatchRenameDialog final : public Flex {
public:
  BatchRenameDialog(
      std::size_t count, std::function<void(luciole::BatchRenameOptions)> preview, std::function<void()> apply,
      std::function<void()> cancel, std::function<void()> close, std::function<void()> copyReport,
      std::function<void()> requestUpdate
  );
  ~BatchRenameDialog() override;
  void sync(const luciole::BatchRenameSnapshot& snapshot, std::size_t completed, bool clipboardAvailable);
  Input* firstInput() const;
  void reveal(const Node* node);
  void tooltipPointer(float x, float y, bool arm);
  void syncTooltip(Renderer& renderer);

private:
  class Adapter;
  void changed();
  luciole::BatchRenameOptions options() const;
  std::unique_ptr<Adapter> m_adapter;
  luciole::BatchRenameSnapshot m_snapshot;
  std::function<void(luciole::BatchRenameOptions)> m_preview;
  Input *m_pattern = nullptr, *m_find = nullptr, *m_replacement = nullptr;
  Stepper *m_start = nullptr, *m_digits = nullptr;
  Toggle* m_extensions = nullptr;
  Segmented* m_mode = nullptr;
  Flex *m_numbering = nullptr, *m_replace = nullptr;
  VirtualGridView* m_grid = nullptr;
  ScrollView* m_scroll = nullptr;
  TooltipPresenter* m_tooltip = nullptr;
  float m_pointerX = -1, m_pointerY = -1;
  Label* m_status = nullptr;
  Button *m_apply = nullptr, *m_cancel = nullptr, *m_report = nullptr;
};
