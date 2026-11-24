#pragma once

#include "fs/archive_type.h"
#include "fs/file_error.h"
#include "fs/file_icon_source.h"
#include "operations/transfer_service.h"

#include <chrono>
#include <optional>
#include <span>
#include <string_view>

namespace luciole {
  enum class ArchiveFormat { Zip, Tar, TarGzip, TarBzip2, TarZstd, TarXz, SevenZip };
  enum class ArchiveEntryKind { File, Directory, Symlink, Hardlink };
  enum class ArchivePhase { Idle, Reading, Compressing, Extracting, Transferring, Cleaning, WaitingPassword };
  struct ArchiveFormatInfo {
    ArchiveFormat format;
    std::string_view label;
    std::string_view suffix;
    ArchiveCompression compression = ArchiveCompression::None;
  };
  struct ArchiveEntry {
    std::string path, parent, name, displayName;
    ArchiveEntryKind kind = ArchiveEntryKind::File;
    std::optional<std::uint64_t> size;
    std::string link;
    unsigned mode = 0644;
    FileIconSource icon{};
    bool encrypted = false;
  };
  struct ArchiveCatalog {
    Location source;
    std::string stamp, format;
    std::vector<ArchiveEntry> entries;
    std::uint64_t expandedBytes = 0;
    bool encrypted = false;
  };
  struct ArchiveLimits {
    std::uint64_t maxArchiveBytes = 4ULL * 1024 * 1024 * 1024;
    std::uint64_t maxExpandedBytes = 8ULL * 1024 * 1024 * 1024;
    std::size_t maxEntries = 50000;
    std::size_t maxMetadataBytes = 16U * 1024 * 1024;
    unsigned maxDepth = 128;
    std::chrono::seconds maxTime{120};
  };
  std::span<const ArchiveFormatInfo> archiveFormats();
  bool archiveEncryptionAvailable();
  std::optional<std::string>
  archivePasswordProblem(std::string_view password, ArchiveFormat format = ArchiveFormat::Zip);
  struct ArchivePasswordRequest {
    std::uint64_t id;
    Location source;
    bool retry;
    ArchiveFormat format = ArchiveFormat::Zip;
  };

  // One archive job, including staged publication through the existing transfer service.
  class ArchiveService {
  public:
    explicit ArchiveService(ArchiveLimits limits = {});
    ~ArchiveService();
    ArchiveService(const ArchiveService&) = delete;
    ArchiveService& operator=(const ArchiveService&) = delete;
    bool inspect(Location source);
    bool
    compress(std::vector<Location> sources, Location destinationFile, ArchiveFormat format, std::string password = {});
    // `expected` is a catalog of `source` read earlier, by this or another instance; extraction fails when the
    // archive no longer matches it. Without one, the catalog this instance read is used.
    bool extract(
        Location source, Location destinationFolder, std::vector<std::string> selected = {},
        std::shared_ptr<const ArchiveCatalog> expected = nullptr
    );
    bool openMember(const std::string& path);
    void cancel();
    void shutdown();
    bool busy() const;
    ArchivePhase phase() const;
    std::uint64_t bytes() const;
    std::size_t items() const;
    const std::optional<FileError>& error() const;
    const std::shared_ptr<const ArchiveCatalog>& catalog() const;
    std::optional<Location> takeOpenedFile();
    const std::vector<TransferResult>& results() const;
    std::optional<TransferConflict> conflict() const;
    bool resolve(std::uint64_t id, TransferConflictChoice choice);
    std::optional<ArchivePasswordRequest> passwordRequest() const;
    bool providePassword(std::uint64_t id, std::string password);
    void setChangedCallback(std::function<void()> callback);

  private:
    struct State;
    std::shared_ptr<State> m_state;
  };
} // namespace luciole
