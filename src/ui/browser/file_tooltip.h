#pragma once

#include "fs/directory_session.h"
#include "render/scene/tooltip_content.h"

#include <functional>
#include <memory>

namespace luciole {
  struct FileTooltipTarget {
    std::string context;
    DirectoryEntry entry;
    std::string typeLabel;
  };

  bool sameTooltipFile(const FileTooltipTarget& a, const FileTooltipTarget& b);

  // One visible file per window. Replies borrow neither scene nodes nor model entries.
  class FileTooltipData {
  public:
    explicit FileTooltipData(std::function<void()> changed);
    ~FileTooltipData();
    FileTooltipData(const FileTooltipData&) = delete;
    FileTooltipData& operator=(const FileTooltipData&) = delete;
    void show(const FileTooltipTarget& target);
    void refresh();
    void clear();
    std::vector<TooltipRow> rows() const;

  private:
    struct State;
    std::shared_ptr<State> m_state;
  };
} // namespace luciole
