#pragma once

#include "fs/location.h"
#include "render/scene/node.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luciole {
  class FileIconCache;
  class FileLauncher;
  struct DirectoryEntry;
} // namespace luciole

struct KeyboardEvent;
class ArchiveView;
class OperationJob;
class PageContext;
class Renderer;
class WaylandConnection;

// Archives: the compress and extract dialogs, extracting next to the archive, and the full-page archive browser.
class ArchiveController {
public:
  ArchiveController(
      PageContext& page, luciole::FileLauncher& launcher, luciole::FileIconCache& icons, WaylandConnection& connection
  );

  void compress(const std::vector<luciole::DirectoryEntry>& targets);
  void extractTo(const luciole::Location& archive);
  void extractHere(const luciole::Location& archive);
  void browse(const luciole::Location& archive);
  // Owned archive job change: opens an extracted member, reports a finished operation and syncs the archive
  // browser on the next frame.
  void changed(OperationJob& job);
  // Frame slot in BrowserPage::prepareFrame().
  void onFrame();
  // Removes the archive browser now.
  void close();
  // True while the archive browser overlay is shown.
  bool open() const { return m_view != nullptr; }
  // Keyboard hook in BrowserPage::keyboard(): handles every key while the archive browser is shown.
  bool key(const KeyboardEvent& event);
  // Layout hook in BrowserPage::prepareFrame().
  void layout(Renderer& renderer, LayoutRect bounds);

private:
  static luciole::Location destination(const luciole::Location& source);
  void openDialog(std::vector<luciole::Location> sources, std::vector<std::string> selected, bool compress);
  bool
  extract(const luciole::Location& source, const luciole::Location& destination, std::vector<std::string> selected);
  // The job the archive browser shows: the newest running archive job of the page, else its own.
  OperationJob* shown() const;
  void deferClose();
  void sync();
  void syncPassword();
  struct PasswordDialog {
    std::uint64_t job, request;
  };
  std::optional<PasswordDialog> m_passwordDialog;

  PageContext& m_page;
  // The archive browser's job: reads the catalog and opens members. Compress and extract jobs are separate.
  std::shared_ptr<OperationJob> m_browser;
  luciole::FileLauncher& m_launcher;
  luciole::FileIconCache& m_icons;
  WaylandConnection& m_connection;
  ArchiveView* m_view = nullptr;
  std::optional<luciole::Location> m_source;
  bool m_wanted = false;
  bool m_dirty = false;
  std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
};
