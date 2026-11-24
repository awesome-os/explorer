#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace luciole {
  struct FilePattern {
    enum class Kind : std::uint8_t { Glob = 0, MimeType = 1 };
    Kind kind = Kind::Glob;
    std::string pattern;
    bool operator==(const FilePattern&) const = default;
  };

  // FileChooser portal filter semantics: case-sensitive globs, MIME types with subclassing and `type/*`.
  class FileFilter {
  public:
    explicit FileFilter(std::vector<FilePattern> patterns);
    // `contentType`: the entry's known content type, nullptr when unknown (then guessed from `name`).
    bool matches(std::string_view name, const char* contentType) const;

  private:
    std::vector<FilePattern> m_patterns;
  };
} // namespace luciole
