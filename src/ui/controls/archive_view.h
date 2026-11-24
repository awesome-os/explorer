#pragma once

#include "operations/archive_service.h"
#include "thumbnails/file_icon_cache.h"
#include "ui/controls/flex.h"
#include "ui/controls/virtual_grid_view.h"

#include <set>

class Label;
class Button;
class ArchiveView final : public Flex {
public:
  ArchiveView(
      luciole::FileIconCache& icons, std::function<std::uint32_t()> modifiers, std::function<void()> close,
      std::function<void(std::vector<std::string>)> extract, std::function<void(const std::string&)> open,
      std::function<void()> reload, std::function<void()> cancel
  );
  ~ArchiveView() override;
  void sync(
      std::shared_ptr<const luciole::ArchiveCatalog> catalog, luciole::ArchivePhase phase, std::size_t items,
      std::uint64_t bytes, const std::optional<luciole::FileError>& error
  );
  bool key(std::uint32_t symbol, std::uint32_t modifiers);
  InputArea* focusArea() const;

private:
  class Adapter;
  void rebuild();
  void choose(std::size_t index, std::uint32_t modifiers);
  void activate(std::size_t index);
  void up();
  void selectionChanged();
  void doLayout(Renderer& renderer) override;
  std::unique_ptr<Adapter> m_adapter;
  std::shared_ptr<const luciole::ArchiveCatalog> m_catalog;
  std::vector<std::size_t> m_rows;
  std::set<std::string> m_selection;
  std::string m_parent;
  std::optional<std::size_t> m_focus, m_anchor;
  std::function<std::uint32_t()> m_modifiers;
  std::function<void(std::vector<std::string>)> m_extract;
  std::function<void(const std::string&)> m_open;
  VirtualGridView* m_grid = nullptr;
  InputArea* m_shield = nullptr;
  Label *m_title = nullptr, *m_path = nullptr, *m_status = nullptr;
  Button *m_up = nullptr, *m_extractAll = nullptr, *m_extractSelected = nullptr, *m_openButton = nullptr,
         *m_cancel = nullptr, *m_reload = nullptr;
  bool m_busy = false;
};
