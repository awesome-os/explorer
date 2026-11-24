#pragma once

#include <filesystem>
#include <glib.h>
#include <memory>

namespace luciole {
  class DirectoryWatch;

  // Applies $XDG_CONFIG_HOME/luciole/colors.toml and re-applies it whenever it is written, so a Noctalia
  // template keeps the window in sync with the desktop theme. A missing file restores the built-in palette;
  // an invalid one is logged and leaves the current palette in place.
  class PaletteWatcher {
  public:
    PaletteWatcher();
    ~PaletteWatcher();
    PaletteWatcher(const PaletteWatcher&) = delete;
    PaletteWatcher& operator=(const PaletteWatcher&) = delete;

  private:
    void reload();
    void scheduleReload();
    // Watches the config folder, or its parent until the folder exists.
    void watch();

    std::filesystem::path m_path;
    std::filesystem::path m_watched;
    std::unique_ptr<DirectoryWatch> m_monitor;
    GSource* m_debounce = nullptr;
  };
} // namespace luciole
