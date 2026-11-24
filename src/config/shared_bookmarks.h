#pragma once

#include "config/state_store.h"

namespace luciole {
  // GTK's shared URI[ label] file. Worker-owned; the shared document is authoritative.
  class SharedBookmarks {
  public:
    SharedBookmarks(std::filesystem::path path, std::filesystem::path legacy);
    static std::filesystem::path pathFromEnvironment();
    static std::filesystem::path legacyFromEnvironment();
    bool load();
    AtomicWriteResult save(const std::vector<Bookmark>& bookmarks);
    const std::vector<Bookmark>& bookmarks() const { return m_bookmarks; }
    const std::string& error() const { return m_error; }
    std::vector<std::filesystem::path> watchPaths() const;

  private:
    struct Line {
      std::string raw;
      std::optional<Bookmark> bookmark;
      bool duplicate = false;
    };
    void parse(const std::string& text);
    std::string format(const std::vector<Bookmark>& bookmarks) const;
    std::filesystem::path m_path;
    std::filesystem::path m_legacy;
    std::optional<std::string> m_expected;
    std::vector<Line> m_lines;
    std::vector<Bookmark> m_bookmarks;
    std::string m_error;
    bool m_loaded = false;
    bool m_seenModern = false;
  };
} // namespace luciole
