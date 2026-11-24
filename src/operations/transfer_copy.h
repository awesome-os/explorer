#pragma once

#include "fs/cancel_token.h"
#include "fs/file_error.h"
#include "fs/posix_fd.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

// Native-filesystem copy primitives for transfer jobs. Everything here runs on the
// worker thread and works on descriptors relative to pinned parent folders.
namespace luciole {
  class HistoryRevisions;
}
namespace luciole::transfer {
  struct TransferCancelled : FileErrorException {
    TransferCancelled() : FileErrorException({.kind = FileErrorKind::Cancelled, .message = "Transfer cancelled."}) {}
  };

  // Throws errno as "<operation>: <strerror>".
  [[noreturn]] void fail(const char* operation);
  Fd opened(int fd);
  // lstat of name inside parent.
  struct stat info(int parent, const std::string& name);
  bool unchanged(const struct stat& a, const struct stat& b);
  bool sameContent(const struct stat& a, const struct stat& b);
  void requireUnchanged(const struct stat& expected, const struct stat& actual);
  Fd directory(int parent, const std::string& name);
  std::vector<std::string> children(int fd, std::size_t limit = std::numeric_limits<std::size_t>::max());
  bool hasAncestor(int fd, const struct stat& ancestor);

  // Per-job copy state shared by every item of one run.
  struct CopyWork {
    CopyWork(CancelToken cancel, std::atomic<std::uint64_t>& copied)
        : token(std::move(cancel)), bytes(copied), buffer(128 * 1024) {}
    CancelToken token;
    std::atomic<std::uint64_t>& bytes;
    // Metadata of items this job renamed or unlinked, so later checks accept the job's own changes.
    std::map<std::pair<dev_t, ino_t>, struct stat> changedLinks;
    std::shared_ptr<HistoryRevisions> history;
    std::vector<char> buffer;
    bool metadataIncomplete = false;
    // Kernel copy paths the destination filesystem rejected; later files skip straight to the next one.
    bool cloneUnsupported = false;
    bool rangeUnsupported = false;
    struct stat expected(const struct stat& original) const;
    void note(const struct stat& value);
    void noteUnlinked(const struct stat& value);
    void check() const;
  };

  struct Tree {
    std::string name;
    struct stat metadata;
    std::vector<Tree> children;
  };
  Tree copyTree(int from, const std::string& name, int to, const std::string& target, CopyWork& work, unsigned depth);
  void verifyTree(int parent, const Tree& tree, CopyWork& work);
  void removeSource(int parent, const Tree& tree, CopyWork& work);
  void removeTemporary(int parent, const std::string& name, CopyWork& work);
  // Makes everything written to `fd`'s filesystem durable. Copies skip per-file syncs; this runs once before a
  // transfer deletes data that the copy replaces (moved sources, replaced destinations).
  void synchronize(int fd);
} // namespace luciole::transfer
