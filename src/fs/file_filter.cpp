#include "fs/file_filter.h"

#include "fs/gio_ptr.h"

#include <algorithm>
#include <fnmatch.h>
#include <gio/gio.h>
#include <utility>

luciole::FileFilter::FileFilter(std::vector<FilePattern> patterns) : m_patterns(std::move(patterns)) {}

bool luciole::FileFilter::matches(std::string_view name, const char* contentType) const {
  const std::string text(name);
  luciole::GString guessed;
  const auto type = [&] -> const char* {
    if (contentType) {
      return contentType;
    }
    if (!guessed) {
      guessed.reset(g_content_type_guess(text.c_str(), nullptr, 0, nullptr));
    }
    return guessed.get();
  };
  return std::ranges::any_of(m_patterns, [&](const FilePattern& pattern) {
    if (pattern.kind == FilePattern::Kind::Glob) {
      return ::fnmatch(pattern.pattern.c_str(), text.c_str(), 0) == 0;
    }
    const char* actual = type();
    if (!actual) {
      return false;
    }
    if (pattern.pattern.ends_with("/*")) {
      const std::string_view prefix(pattern.pattern.data(), pattern.pattern.size() - 1);
      return std::string_view(actual).starts_with(prefix);
    }
    return g_content_type_is_a(actual, pattern.pattern.c_str()) != FALSE;
  });
}
