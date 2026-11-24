#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace luciole {
  enum class AtomicWriteState : unsigned char {
    Unchanged,
    NotCommitted,
    Committed, // Visible replacement, but directory sync failed. Do not report rollback.
    Durable,
  };

  struct AtomicWriteResult {
    AtomicWriteState state = AtomicWriteState::NotCommitted;
    std::string error;
  };

  // nullopt leaves the file unchanged; an error aborts before replacement.
  using AtomicTransform =
      std::function<std::expected<std::optional<std::string>, std::string>(const std::optional<std::string>&)>;

  // For private, application-owned local files. Serializes cooperating writers
  // with a persistent advisory lock. Call outside Render, on an I/O worker.
  [[nodiscard]] AtomicWriteResult
  updateTextFileAtomic(const std::filesystem::path& path, const AtomicTransform& transform);

  // No creation or chmod during reads; missing files/parents return nullopt.
  // Owned regular files only, without symlinks or hard links. Read-only is OK.
  std::expected<std::optional<std::string>, std::string> readPrivateTextFile(const std::filesystem::path& path);
} // namespace luciole
