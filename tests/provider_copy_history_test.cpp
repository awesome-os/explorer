#include "event_loop/glib_poll_source.h"
#include "fs/gio_interop.h"
#include "operations/provider_copy_receipt.h"
#include "operations/provider_transfer.h"
#include "test_check.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
  using namespace luciole;
  namespace fs = std::filesystem;
  using enum TransferConflictChoice;
  using enum TransferOutcome;
  void write(const fs::path& path, const std::string& bytes) { std::ofstream(path) << bytes; }
  std::string read(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), {}};
  }
  void wait(TransferService& service, const ProviderTransfer::Choose& choose = {}) {
    const auto deadline = g_get_monotonic_time() + 20000000;
    while (service.busy()) {
      while (g_main_context_iteration(nullptr, false)) {
      }
      if (const auto conflict = service.conflict()) {
        TEST_CHECK(choose);
        TEST_CHECK(service.resolve(conflict->id, choose(*conflict)));
      }
      TEST_CHECK(g_get_monotonic_time() < deadline);
      g_usleep(1000);
    }
  }
  void drain(bool errors = false) {
    const auto deadline = g_get_monotonic_time() + 10000000;
    while (CopyReceipt::cleanupPending()) {
      while (g_main_context_iteration(nullptr, false)) {
      }
      TEST_CHECK(g_get_monotonic_time() < deadline);
      g_usleep(1000);
    }
    const auto messages = CopyReceipt::takeCleanupErrors();
    for (const auto& error : messages) {
      std::println(stderr, "cleanup: {}", error.message);
    }
    TEST_CHECK(errors ? !messages.empty() : messages.empty());
  }
  struct Result {
    std::vector<TransferResult> outcomes;
    std::shared_ptr<const CopyReceipt> receipt;
  };
  Result upload(
      const std::vector<Location>& sources, const Location& destination,
      const ProviderTransfer::Choose& choose = [](const auto&) { return KeepBoth; },
      TransferAction action = TransferAction::Copy
  ) {
    const TransferRequest request{sources, destination, action};
    if (destination.isNative()) {
      ProviderTransfer provider(request);
      std::atomic<std::uint64_t> bytes{0};
      std::atomic<std::size_t> completed{0};
      auto results = provider.run({}, bytes, completed, choose, nullptr, true);
      return {std::move(results), provider.takeCopyReceipt({})};
    }
    TransferService service;
    TEST_CHECK(service.start(request));
    wait(service, choose);
    return {service.results(), service.takeCopyReceipt()};
  }
  Result replay(const std::shared_ptr<const CopyReceipt>& receipt) {
    TEST_CHECK(receipt);
    TransferService service;
    TEST_CHECK(service.replayCopy(receipt));
    wait(service);
    return {service.results(), service.takeCopyReceipt()};
  }
  std::shared_ptr<const CopyReceipt> reversed(const std::shared_ptr<const CopyReceipt>& receipt) {
    auto result = replay(receipt);
    for (const auto& outcome : result.outcomes) {
      if (outcome.error) {
        std::println(stderr, "replay: {}", outcome.error->message);
      }
      TEST_CHECK(outcome.outcome == Moved && !outcome.error);
    }
    TEST_CHECK(result.receipt);
    return result.receipt;
  }
  void mount(const Location& root) {
    const GioPtr<GMountOperation> operation(g_mount_operation_new());
    g_signal_connect(
        operation.get(), "ask-question",
        G_CALLBACK(+[](GMountOperation* op, const char*, const char* const*, gpointer) {
          g_mount_operation_set_choice(op, 0);
          g_mount_operation_reply(op, G_MOUNT_OPERATION_HANDLED);
        }),
        nullptr
    );
    g_signal_connect(
        operation.get(), "ask-password",
        G_CALLBACK(+[](GMountOperation* op, const char*, const char*, const char*, GAskPasswordFlags flags, gpointer) {
          if ((flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) != 0) {
            g_mount_operation_set_anonymous(op, true);
            g_mount_operation_reply(op, G_MOUNT_OPERATION_HANDLED);
          } else {
            g_mount_operation_reply(op, G_MOUNT_OPERATION_ABORTED);
          }
        }),
        nullptr
    );
    std::optional<bool> result;
    g_file_mount_enclosing_volume(
        gioFile(root).get(), G_MOUNT_MOUNT_NONE, operation.get(), nullptr,
        [](GObject* file, GAsyncResult* reply, gpointer data) {
          GError* error = nullptr;
          const bool ok = g_file_mount_enclosing_volume_finish(G_FILE(file), reply, &error);
          *static_cast<std::optional<bool>*>(data) =
              ok || (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED));
          if (!ok && error) {
            std::println(stderr, "mount: {}", error->message);
          }
          g_clear_error(&error);
        },
        &result
    );
    const auto deadline = g_get_monotonic_time() + 15000000;
    while (!result) {
      while (g_main_context_iteration(nullptr, false)) {
      }
      TEST_CHECK(g_get_monotonic_time() < deadline);
      g_usleep(1000);
    }
    TEST_CHECK(*result);
  }
  void noStages(const fs::path& path) {
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
      TEST_CHECK(!entry.path().filename().native().starts_with(".luciole-"));
    }
  }
  fs::path anchor(const fs::path& path) {
    for (const auto& entry : fs::directory_iterator(path)) {
      if (entry.path().filename().native().starts_with(".luciole-history-")) {
        return entry.path();
      }
    }
    TEST_CHECK(false);
    return {};
  }
  void localRetention(const fs::path& local, const fs::path& backend) {
    for (unsigned variant = 0; variant < 3; ++variant) {
      const auto name = std::to_string(variant);
      const auto staged = local / name / "staged";
      const auto target = backend / name / "café item";
      const auto old = backend / name / "previous";
      fs::create_directories(staged / "nested");
      fs::create_directories(target.parent_path());
      write(staged / "nested/content", "incoming local history");
      fs::create_directory(staged / "empty");
      fs::create_symlink("missing", staged / "dangling");
      if (variant) {
        fs::create_directories(target / "nested");
        write(target / "nested/content", "previous local history");
      }
      fs::path retained;
      {
        provider_history::Budget budget;
        auto effect = ProviderCopyEffect::prepareLocal(
            Location::fromPath(target.native()), Location::fromPath(staged.native()), budget, {}, variant != 0,
            Location::fromPath(target.parent_path().native())
        );
        if (variant) {
          fs::rename(target, old);
        }
        fs::rename(staged, target);
        effect->published({}, variant ? std::optional(Location::fromPath(old.native())) : std::nullopt);
        auto undo = ProviderCopyReceipt::capture({effect}, {});
        TEST_CHECK(undo);
        effect.reset();
        fs::remove_all(old);
        auto result = replay(undo);
        TEST_CHECK(result.receipt && result.outcomes.front().outcome == Moved);
        auto redo = result.receipt;
        if (!variant) {
          retained = fs::path(*result.outcomes.front().destination->nativePath()).parent_path().parent_path();
          struct stat mode{};
          TEST_CHECK(::lstat(retained.c_str(), &mode) == 0 && (mode.st_mode & 0777U) == 0700U);
          TEST_CHECK(!fs::exists(target));
        } else {
          TEST_CHECK(read(target / "nested/content") == "previous local history");
        }
        for (unsigned turn = 0; turn < 3; ++turn) {
          undo = reversed(redo);
          TEST_CHECK(read(target / "nested/content") == "incoming local history");
          TEST_CHECK(fs::is_empty(target / "empty") && fs::read_symlink(target / "dangling") == "missing");
          redo = reversed(undo);
          TEST_CHECK(!variant ? !fs::exists(target) : read(target / "nested/content") == "previous local history");
        }
        if (!variant) {
          const auto bytes = retained / "item/café item/nested/content";
          const auto stamp = fs::last_write_time(bytes);
          write(bytes, "modified local history");
          fs::last_write_time(bytes, stamp);
          const auto blocked = replay(redo);
          TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
          TEST_CHECK(!fs::exists(target));
        } else if (variant == 2) {
          undo = reversed(redo);
        }
      }
      drain(variant == 0);
      if (!variant) {
        TEST_CHECK(read(retained / "item/café item/nested/content") == "modified local history");
        fs::remove_all(retained);
      }
      noStages(target.parent_path());
      TEST_CHECK(variant != 2 || read(target / "nested/content") == "incoming local history");
    }
  }
  void linkNamespaces() {
    const auto from = [](const std::string& uri) { return Location::fromUri(uri); };
    TEST_CHECK(
        transferLinkTarget(from("sftp://alice@host/a%20b%25%23"), from("sftp://alice@host:22/else")) == "/a b%#"
    );
    const auto source = from("sftp://alice@host/data");
    for (const auto& uri :
         {"sftp://bob@host/else", "sftp://host/else", "sftp://alice@other/else", "sftp://alice@host:2222/else",
          "smb://host/share", "dav://host/else", "file:///tmp"}) {
      TEST_CHECK(!transferLinkTarget(source, from(uri)));
    }
    TEST_CHECK(!transferLinkTarget(from("file:///tmp/a"), from("sftp://alice@host/tmp")));
    TEST_CHECK(!transferLinkTarget(from("smb://host/share/a"), from("smb://host/share/b")));
    TEST_CHECK(!transferLinkTarget(from("davs://host/a"), from("davs://host/b")));
    TEST_CHECK(!transferLinkTarget(from("sftp://alice:secret@host/a"), from("sftp://alice@host/b")));
    TEST_CHECK(!transferLinkTarget(from("sftp://alice@host/a?query"), from("sftp://alice@host/b")));
    for (const auto& uri : {"sftp://alice@host/b", "smb://host/share", "dav://host/b"}) {
      ProviderTransfer denied({{Location::fromPath("/tmp/no-provider-link-source")}, from(uri), TransferAction::Link});
      std::atomic<std::uint64_t> bytes{0};
      std::atomic<std::size_t> completed{0};
      const auto result = denied.run({}, bytes, completed, {}, nullptr, true);
      TEST_CHECK(result.front().outcome == Failed && result.front().error->kind == FileErrorKind::Unsupported);
      TEST_CHECK(bytes == 0 && completed == 1 && !denied.takeCopyReceipt({}));
    }
  }
  void linkHistory(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/tree");
    fs::create_directories(backend / "destination");
    const auto source = remote.child("source"), destination = remote.child("destination");
    const std::string raw = std::string("raw-") + static_cast<char>(0xff);
    const std::vector<std::string> names{"one.txt", "café % #.txt", raw, "tree", "alias"};
    write(backend / "source/one.txt", "original");
    write(backend / "source/café % #.txt", "unicode");
    write(backend / "source" / raw, "raw bytes");
    // A symbolic link to a folder does not copy or enumerate its special children.
    TEST_CHECK(::mkfifo((backend / "source/tree/fifo").c_str(), 0600) == 0);
    fs::create_symlink("absent", backend / "source/alias");
    std::vector<Location> selected;
    selected.reserve(names.size());
    for (const auto& name : names) {
      selected.push_back(source.child(name));
    }
    {
      auto linked = upload(selected, destination, {}, TransferAction::Link);
      for (const auto& item : linked.outcomes) {
        if (item.error) {
          std::println(stderr, "link: {}", item.error->message);
        }
        TEST_CHECK(item.outcome == Linked && !item.error);
      }
      TEST_CHECK(linked.receipt && linked.receipt->size() == names.size());
      TEST_CHECK(linked.receipt->action() == TransferAction::Link);
      for (const auto& name : names) {
        TEST_CHECK(fs::is_symlink(fs::symlink_status(backend / "destination" / name)));
        TEST_CHECK(fs::read_symlink(backend / "destination" / name) == backend / "source" / name);
      }
      fs::remove_all(backend / "source/tree");
      fs::remove(backend / "source/one.txt");
      auto redo = reversed(linked.receipt);
      TEST_CHECK(redo->action() == TransferAction::Link);
      write(backend / "destination/one.txt", "later occupant");
      auto refused = replay(redo);
      TEST_CHECK(!refused.receipt && refused.outcomes.front().outcome == Failed);
      TEST_CHECK(read(backend / "destination/one.txt") == "later occupant");
      fs::remove(backend / "destination/one.txt");
      auto undo = reversed(redo);
      TEST_CHECK(undo->action() == TransferAction::Link);
      TEST_CHECK(fs::is_symlink(fs::symlink_status(backend / "destination/one.txt")));
      TEST_CHECK(!fs::exists(backend / "destination/one.txt"));
      redo = reversed(undo);
      for (const auto& name : names) {
        TEST_CHECK(!fs::is_symlink(fs::symlink_status(backend / "destination" / name)));
      }
    }
    drain();
    noStages(backend);
    write(backend / "source/one.txt", "new source");
    {
      write(backend / "destination/one.txt", "predecessor");
      auto linked = upload(
          {source.child("one.txt")}, destination,
          [](const auto& conflict) {
            TEST_CHECK(!conflict.canMerge);
            return Replace;
          },
          TransferAction::Link
      );
      TEST_CHECK(linked.receipt && linked.outcomes.front().outcome == Linked);
      auto redo = reversed(linked.receipt);
      TEST_CHECK(read(backend / "destination/one.txt") == "predecessor");
      auto undo = reversed(redo);
      TEST_CHECK(fs::is_symlink(backend / "destination/one.txt"));
      fs::remove(backend / "destination/one.txt");
      fs::create_symlink("tampered", backend / "destination/one.txt");
      auto refused = replay(undo);
      TEST_CHECK(!refused.receipt && refused.outcomes.front().outcome == Failed);
      TEST_CHECK(fs::read_symlink(backend / "destination/one.txt") == "tampered");
    }
    drain(true);
    const auto preserved = anchor(backend / "destination");
    TEST_CHECK(read(preserved / "previous") == "predecessor");
    fs::remove_all(preserved); // Only this fixture's independently verified private retained data.
    noStages(backend);
    fs::remove(backend / "destination/one.txt");
    {
      auto linked = upload({source.child("one.txt")}, source, {}, TransferAction::Link);
      TEST_CHECK(linked.receipt && linked.outcomes.front().outcome == Linked);
      TEST_CHECK(linked.outcomes.front().destination == source.child("one (1).txt"));
      TEST_CHECK(read(backend / "source/one.txt") == "new source");
      auto redo = reversed(linked.receipt);
      TEST_CHECK(!fs::exists(backend / "source/one (1).txt"));
    }
    drain();
    noStages(backend);
    {
      fs::create_directories(backend / "source/parent/child");
      auto linked = upload(
          {source.child("parent"), source.child("parent").child("child")}, destination, {}, TransferAction::Link
      );
      TEST_CHECK(linked.receipt && linked.receipt->size() == 2);
      TEST_CHECK(linked.outcomes[0].outcome == Linked && linked.outcomes[1].outcome == Linked);
      auto redo = reversed(linked.receipt);
    }
    drain();
    noStages(backend);
    {
      // Link into the source directory itself without treating directory membership as copied content.
      auto linked = upload({source.child("parent")}, source.child("parent"), {}, TransferAction::Link);
      TEST_CHECK(linked.receipt && linked.outcomes.front().outcome == Linked);
      TEST_CHECK(fs::read_symlink(backend / "source/parent/parent") == backend / "source/parent");
      auto redo = reversed(linked.receipt);
    }
    drain();
    noStages(backend);
    {
      fs::create_directories(backend / "destination/parent");
      write(backend / "destination/parent/keep", "replaced folder content");
      auto linked = upload(
          {source.child("parent")}, destination,
          [](const auto& conflict) {
            TEST_CHECK(!conflict.canMerge && conflict.sourceDirectory && conflict.destinationDirectory);
            return Replace;
          },
          TransferAction::Link
      );
      TEST_CHECK(linked.receipt && linked.outcomes.front().outcome == Linked);
      auto redo = reversed(linked.receipt);
      TEST_CHECK(read(backend / "destination/parent/keep") == "replaced folder content");
      auto undo = reversed(redo);
      TEST_CHECK(fs::is_symlink(backend / "destination/parent"));
    }
    drain();
    noStages(backend);
    {
      auto linked = upload({source.child("one.txt"), source.child("alias")}, destination, {}, TransferAction::Link);
      TEST_CHECK(linked.receipt && linked.receipt->size() == 2);
      const auto first = fs::read_symlink(backend / "destination/one.txt");
      fs::remove(backend / "destination/alias");
      fs::create_symlink("edited", backend / "destination/alias");
      auto refused = replay(linked.receipt);
      TEST_CHECK(!refused.receipt && refused.outcomes.front().outcome == Failed);
      TEST_CHECK(fs::read_symlink(backend / "destination/one.txt") == first);
    }
    drain();
    noStages(backend);
    fs::remove(backend / "destination/one.txt");
    {
      write(backend / "destination/one.txt", "keep");
      auto skipped =
          upload({source.child("one.txt")}, destination, [](const auto&) { return Skip; }, TransferAction::Link);
      TEST_CHECK(!skipped.receipt && skipped.outcomes.front().outcome == Skipped);
      auto cancelled =
          upload({source.child("one.txt")}, destination, [](const auto&) { return Cancel; }, TransferAction::Link);
      TEST_CHECK(!cancelled.receipt && cancelled.outcomes.front().outcome == Cancelled);
      TEST_CHECK(read(backend / "destination/one.txt") == "keep");
    }
    drain();
    noStages(backend);
  }

  void lostLinkReply(const Location& remote, const fs::path& backend, const fs::path& arm) {
    fs::create_directories(backend / "source");
    fs::create_directories(backend / "destination");
    write(backend / "source/file", "source remains intact");
    auto linked = upload({remote.child("source").child("file")}, remote.child("destination"), {}, TransferAction::Link);
    TEST_CHECK(linked.receipt && linked.receipt->action() == TransferAction::Link);
    const auto store = anchor(backend / "destination");
    write(arm, (backend / "destination/file").native());
    auto result = replay(linked.receipt);
    TEST_CHECK(!result.receipt && result.outcomes.front().outcome == Partial);
    TEST_CHECK(read(backend / "source/file") == "source remains intact");
    TEST_CHECK(fs::is_symlink(store / "item"));
    TEST_CHECK(fs::read_symlink(store / "item") == backend / "source/file");
    TEST_CHECK(result.outcomes.front().error->message.contains("Inspect"));
    linked.receipt.reset();
    drain(true);
    TEST_CHECK(fs::is_symlink(store / "item"));
  }

  void basic(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "source/tree/nested");
    fs::create_directories(backend);
    write(local / "source/tree/nested/file.txt", "tree content");
    write(local / "source/tree/.hidden", "hidden content");
    fs::create_symlink("missing target", local / "source/tree/dangling");
    write(local / "source/café.txt", "file content");
    fs::create_directory(local / "source/tree/empty");
    const std::string raw = std::string("raw-") + static_cast<char>(0xff);
    write(local / "source" / raw, "raw content");
    const auto source = Location::fromPath((local / "source").native());
    {
      auto done = upload({source.child("tree"), source.child("café.txt"), source.child(raw)}, remote);
      for (const auto& value : done.outcomes) {
        if (value.error) {
          std::println(stderr, "upload: {}", value.error->message);
        }
        TEST_CHECK(value.outcome == Copied && !value.error);
      }
      TEST_CHECK(done.receipt && done.receipt->size() == 3);
      TEST_CHECK(read(backend / "tree/nested/file.txt") == "tree content");
      struct stat original{};
      TEST_CHECK(::lstat((backend / raw).c_str(), &original) == 0);
      fs::remove_all(local / "source");
      auto redo = reversed(done.receipt);
      TEST_CHECK(!fs::exists(backend / "tree") && !fs::exists(backend / "café.txt"));
      auto undo = reversed(redo);
      TEST_CHECK(read(backend / "tree/nested/file.txt") == "tree content");
      TEST_CHECK(fs::is_symlink(backend / "tree/dangling"));
      TEST_CHECK(fs::is_empty(backend / "tree/empty") && read(backend / raw) == "raw content");
      struct stat restored{};
      TEST_CHECK(::lstat((backend / raw).c_str(), &restored) == 0 && original.st_ino == restored.st_ino);
      const auto stale = replay(done.receipt);
      TEST_CHECK(!stale.receipt && stale.outcomes.front().error->kind == FileErrorKind::Changed);
      redo = reversed(undo);
      undo = reversed(redo);
    }
    drain();
    noStages(backend);
  }
  void collisionsAndEdits(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "source");
    fs::create_directories(backend);
    write(local / "source/file.txt", "original bytes");
    const auto source = Location::fromPath((local / "source/file.txt").native());
    {
      auto done = upload({source}, remote);
      TEST_CHECK(done.receipt);
      auto redo = reversed(done.receipt);
      write(backend / "file.txt", "unrelated occupant");
      auto blocked = replay(redo);
      TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Exists);
      TEST_CHECK(read(backend / "file.txt") == "unrelated occupant");
      fs::remove(backend / "file.txt");
      auto undo = reversed(redo);
      // Preserve the provider's ordinary metadata: the digest must still detect this edit.
      const auto time = fs::last_write_time(backend / "file.txt");
      write(backend / "file.txt", "modified bytes");
      fs::last_write_time(backend / "file.txt", time);
      blocked = replay(undo);
      TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
      TEST_CHECK(read(backend / "file.txt") == "modified bytes");
    }
    drain();
    noStages(backend);
    {
      auto numbered = upload({source}, remote);
      TEST_CHECK(numbered.receipt && read(backend / "file (1).txt") == "original bytes");
      auto redo = reversed(numbered.receipt);
      auto undo = reversed(redo);
    }
    drain();
    noStages(backend);
    {
      const auto result = upload({source}, remote, [](const auto&) { return Replace; });
      TEST_CHECK(result.outcomes.front().outcome == Copied && result.receipt);
    }
    drain();
    noStages(backend);
  }
  void replacements(const fs::path& local, const Location& remote, const fs::path& backend) {
    // Replace is distinct from merging: both complete versions must survive repeated replay.
    for (unsigned variant = 0; variant < 5; ++variant) {
      const auto name = std::to_string(variant);
      const auto origin = local / name / "café\nitem";
      const auto destination = backend / name / "café\nitem";
      fs::create_directories(origin.parent_path());
      fs::create_directories(destination.parent_path());
      const bool incomingFolder = variant == 1 || variant == 3;
      const bool previousFolder = variant == 2 || variant == 3;
      if (incomingFolder) {
        fs::create_directories(origin / "nested");
        write(origin / "nested/.new", "incoming tree");
        fs::create_symlink("missing target", origin / "dangling");
      } else {
        write(origin, "incoming file");
      }
      if (previousFolder) {
        fs::create_directories(destination / "nested");
        write(destination / "nested/.old", "previous tree");
        TEST_CHECK(::chmod(destination.c_str(), 0750) == 0);
      } else if (variant == 4) {
        fs::create_symlink("unrelated-target", destination);
        write(destination.parent_path() / "unrelated-target", "leave target alone");
      } else {
        write(destination, "previous file");
      }
      {
        auto done =
            upload({Location::fromPath(origin.native())}, remote.child(name), [](const auto&) { return Replace; });
        TEST_CHECK(done.receipt && done.outcomes.front().outcome == Copied);
        fs::remove_all(origin);
        auto redo = reversed(done.receipt);
        for (unsigned turn = 0; turn < 3; ++turn) {
          if (previousFolder) {
            TEST_CHECK(read(destination / "nested/.old") == "previous tree");
            TEST_CHECK(!fs::exists(destination / "nested/.new"));
            struct stat mode{};
            TEST_CHECK(::lstat(destination.c_str(), &mode) == 0 && (mode.st_mode & 0777U) == 0750U);
          } else if (variant == 4) {
            TEST_CHECK(fs::read_symlink(destination) == "unrelated-target");
            TEST_CHECK(read(destination.parent_path() / "unrelated-target") == "leave target alone");
          } else {
            TEST_CHECK(read(destination) == "previous file");
          }
          auto undo = reversed(redo);
          if (incomingFolder) {
            TEST_CHECK(read(destination / "nested/.new") == "incoming tree");
            TEST_CHECK(fs::read_symlink(destination / "dangling") == "missing target");
          } else {
            TEST_CHECK(read(destination) == "incoming file" && !fs::is_symlink(destination));
          }
          redo = reversed(undo);
        }
      }
      drain();
      noStages(destination.parent_path());
    }
  }
  void replacementRefusal(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "item", "incoming bytes");
    const auto source = Location::fromPath((local / "item").native());
    for (unsigned variant = 0; variant < 3; ++variant) {
      fs::create_directory(backend / std::to_string(variant));
      const auto destination = backend / std::to_string(variant) / "item";
      write(destination, "previous bytes");
      fs::path retained;
      {
        auto done = upload({source}, remote.child(std::to_string(variant)), [](const auto&) { return Replace; });
        TEST_CHECK(done.receipt);
        retained = anchor(destination.parent_path());
        auto receipt = done.receipt;
        if (variant == 2) {
          receipt = reversed(receipt);
        }
        const auto changed = variant == 0 ? destination : retained / (variant == 1 ? "previous" : "item");
        const auto time = fs::last_write_time(changed);
        write(changed, variant == 0 ? "modified bytes" : "tampered bytes");
        fs::last_write_time(changed, time);
        const auto visible = read(destination);
        const auto blocked = replay(receipt);
        TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
        TEST_CHECK(read(destination) == visible);
      }
      drain(true);
      TEST_CHECK(fs::exists(retained / (variant == 2 ? "item" : "previous")));
      fs::remove_all(retained);
    }
    // Both versions count toward the same content budget; an optional proof failure creates no anchor.
    write(backend / "item", "previous bytes");
    provider_history::Budget budget;
    budget.bytes = 20;
    bool refused = false;
    try {
      (void)ProviderCopyEffect::prepare(remote.child("item"), source, budget, {}, true);
    } catch (const FileErrorException& error) {
      refused = error.error().kind == FileErrorKind::Unsupported;
    }
    TEST_CHECK(refused && read(backend / "item") == "previous bytes");
    drain();
    noStages(backend);
  }

  void replacementSelection(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "a", "incoming a");
    write(local / "b", "incoming b");
    write(backend / "b", "previous b");
    {
      auto done = upload(
          {Location::fromPath((local / "a").native()), Location::fromPath((local / "b").native())}, remote,
          [](const auto&) { return Replace; }
      );
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      CancelToken cancel;
      cancel.cancel();
      std::atomic<std::size_t> count{0};
      const auto cancelled = done.receipt->reverse(cancel, count);
      TEST_CHECK(!cancelled.receipt && !cancelled.retireHistory);
      TEST_CHECK(read(backend / "a") == "incoming a" && read(backend / "b") == "incoming b");
      auto redo = reversed(done.receipt);
      TEST_CHECK(!fs::exists(backend / "a") && read(backend / "b") == "previous b");
      auto undo = reversed(redo);
      write(backend / "b", "external b");
      const auto refused = replay(undo);
      TEST_CHECK(!refused.receipt && refused.outcomes.back().error->kind == FileErrorKind::Changed);
      TEST_CHECK(read(backend / "a") == "incoming a" && read(backend / "b") == "external b");
    }
    drain(true);
    const auto retained = anchor(backend);
    TEST_CHECK(read(retained / "previous") == "previous b");
    fs::remove_all(retained);
    noStages(backend);
  }

  void cancellationAndDiscard(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "source");
    fs::create_directories(backend);
    write(local / "source/item", "cancel content");
    const auto source = Location::fromPath((local / "source/item").native());
    {
      auto done = upload({source}, remote);
      TEST_CHECK(done.receipt);
      const CancelToken token;
      token.cancel();
      std::atomic<std::size_t> completed{0};
      const auto reply = done.receipt->reverse(token, completed);
      TEST_CHECK(!reply.receipt && !reply.retireHistory && reply.results.front().outcome == Cancelled);
      TEST_CHECK(read(backend / "item") == "cancel content");
      auto redo = reversed(done.receipt);
      TEST_CHECK(!fs::exists(backend / "item"));
    }
    drain();
    noStages(backend);
    TEST_CHECK(!fs::exists(backend / "item") && read(local / "source/item") == "cancel content");
  }
  void cleanupRefusal(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "source");
    fs::create_directories(backend);
    write(local / "source/item", "owned content");
    const auto source = Location::fromPath((local / "source/item").native());
    fs::path retained;
    {
      auto done = upload({source}, remote);
      TEST_CHECK(done.receipt);
      auto redo = reversed(done.receipt);
      retained = anchor(backend);
      write(retained / "item", "edited retained bytes");
    }
    drain(true);
    TEST_CHECK(read(retained / "item") == "edited retained bytes");
    fs::remove_all(retained);
    {
      auto done = upload({source}, remote);
      TEST_CHECK(done.receipt);
      retained = anchor(backend);
      fs::rename(backend, backend.string() + "-held");
      fs::create_directory(backend);
      const auto reply = replay(done.receipt);
      TEST_CHECK(!reply.receipt && reply.outcomes.front().error);
      TEST_CHECK(
          reply.outcomes.front().error->kind == FileErrorKind::Missing
          || reply.outcomes.front().error->kind == FileErrorKind::Changed
      );
      TEST_CHECK(fs::is_empty(backend));
    }
    drain(true);
    fs::remove(backend);
    fs::rename(backend.string() + "-held", backend);
    TEST_CHECK(read(backend / "item") == "owned content");
    fs::remove_all(retained);
  }
  void budget(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "small", "bytes");
    provider_history::Budget none{.entries = 1, .bytes = 1};
    bool refused = false;
    try {
      (void)ProviderCopyEffect::prepare(
          remote.child("small"), Location::fromPath((local / "small").native()), none, {}
      );
    } catch (const FileErrorException& error) {
      refused = error.error().kind == FileErrorKind::Unsupported;
    }
    TEST_CHECK(refused);
    drain();
    provider_history::Budget ordinary;
    bool missing = false;
    try {
      (void)ProviderCopyEffect::prepare(
          remote.child("missing-parent/item"), Location::fromPath((local / "small").native()), ordinary, {}
      );
    } catch (const FileErrorException& error) {
      missing = error.error().kind == FileErrorKind::Missing;
    }
    TEST_CHECK(missing);
    drain();
    noStages(backend);
  }
  void fullSelection(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "a", "first bytes");
    write(local / "b", "second bytes");
    {
      auto done =
          upload({Location::fromPath((local / "a").native()), Location::fromPath((local / "b").native())}, remote);
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      write(backend / "b", "external edit");
      const auto reply = replay(done.receipt);
      TEST_CHECK(!reply.receipt);
      TEST_CHECK(read(backend / "a") == "first bytes" && read(backend / "b") == "external edit");
    }
    drain();
    noStages(backend);
    fs::create_directories(local / "tree");
    fs::create_directories(backend / "tree");
    write(local / "tree/child", "merged");
    write(backend / "tree/keep", "kept");
    {
      auto done = upload({Location::fromPath((local / "tree").native())}, remote, [](const auto&) { return Merge; });
      TEST_CHECK(done.receipt && done.receipt->size() == 1 && done.outcomes.front().outcome == Copied);
      TEST_CHECK(read(backend / "tree/child") == "merged" && read(backend / "tree/keep") == "kept");
      auto redo = reversed(done.receipt);
      TEST_CHECK(!fs::exists(backend / "tree/child") && read(backend / "tree/keep") == "kept");
      auto undo = reversed(redo);
      TEST_CHECK(read(backend / "tree/child") == "merged" && read(backend / "tree/keep") == "kept");
    }
    drain();
    noStages(backend);
    {
      auto done = upload({Location::fromPath((local / "a").native())}, remote, [](const auto&) { return Skip; });
      TEST_CHECK(!done.receipt && done.outcomes.front().outcome == Skipped);
    }
    drain();
    noStages(backend);
  }
  TransferConflictChoice mergeChoice(const TransferConflict& conflict) {
    if (conflict.sourceDirectory && conflict.destinationDirectory) {
      return Merge;
    }
    if (conflict.name == "skip") {
      return Skip;
    }
    return conflict.name == "numbered" ? KeepBoth : Replace;
  }
  void merges(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "tree/nested");
    fs::create_directories(local / "tree/fresh");
    fs::create_directories(local / "tree/empty");
    fs::create_directories(backend / "tree/nested");
    write(local / "tree/new.txt", "new content");
    write(local / "tree/.hidden", "hidden content");
    write(local / "tree/fresh/café\nfile", "fresh content");
    fs::create_symlink("missing target", local / "tree/dangling");
    write(local / "tree/nested/replaced", "incoming content");
    write(local / "tree/nested/skip", "never copied");
    write(local / "tree/nested/numbered", "numbered content");
    write(local / "extra.txt", "extra content");
    write(backend / "tree/nested/replaced", "previous content");
    write(backend / "tree/nested/skip", "skipped content");
    write(backend / "tree/nested/numbered", "keep numbered original");
    write(backend / "tree/keep", "unrelated content");
    TEST_CHECK(::chmod((backend / "tree").c_str(), 0750) == 0);
    struct stat original{}, nested{}, previous{};
    TEST_CHECK(::lstat((backend / "tree").c_str(), &original) == 0);
    TEST_CHECK(::lstat((backend / "tree/nested").c_str(), &nested) == 0);
    TEST_CHECK(::lstat((backend / "tree/nested/replaced").c_str(), &previous) == 0);
    {
      auto done = upload(
          {Location::fromPath((local / "tree").native()), Location::fromPath((local / "extra.txt").native())}, remote,
          mergeChoice
      );
      TEST_CHECK(done.receipt && done.receipt->size() == 8 && done.outcomes.front().outcome == Copied);
      fs::remove_all(local);
      write(backend / "tree/later", "added after copying");
      write(backend / "tree/nested/later", "nested addition");
      auto redo = reversed(done.receipt);
      for (unsigned turn = 0; turn < 3; ++turn) {
        TEST_CHECK(!fs::exists(backend / "tree/new.txt") && !fs::exists(backend / "extra.txt"));
        TEST_CHECK(!fs::exists(backend / "tree/fresh") && !fs::exists(backend / "tree/empty"));
        TEST_CHECK(!fs::is_symlink(backend / "tree/dangling"));
        TEST_CHECK(!fs::exists(backend / "tree/nested/numbered (1)"));
        TEST_CHECK(read(backend / "tree/nested/replaced") == "previous content");
        struct stat restored{};
        TEST_CHECK(::lstat((backend / "tree/nested/replaced").c_str(), &restored) == 0);
        TEST_CHECK(restored.st_ino == previous.st_ino);
        auto undo = reversed(redo);
        TEST_CHECK(read(backend / "tree/new.txt") == "new content");
        TEST_CHECK(read(backend / "tree/fresh/café\nfile") == "fresh content");
        TEST_CHECK(read(backend / "tree/nested/replaced") == "incoming content");
        TEST_CHECK(read(backend / "tree/nested/numbered (1)") == "numbered content");
        TEST_CHECK(fs::read_symlink(backend / "tree/dangling") == "missing target");
        TEST_CHECK(fs::is_empty(backend / "tree/empty"));
        TEST_CHECK(read(backend / "extra.txt") == "extra content");
        TEST_CHECK(read(backend / "tree/keep") == "unrelated content");
        TEST_CHECK(read(backend / "tree/later") == "added after copying");
        TEST_CHECK(read(backend / "tree/nested/later") == "nested addition");
        TEST_CHECK(read(backend / "tree/nested/skip") == "skipped content");
        TEST_CHECK(read(backend / "tree/nested/numbered") == "keep numbered original");
        TEST_CHECK(::lstat((backend / "tree").c_str(), &restored) == 0);
        TEST_CHECK(restored.st_ino == original.st_ino && (restored.st_mode & 0777U) == 0750U);
        TEST_CHECK(::lstat((backend / "tree/nested").c_str(), &restored) == 0 && restored.st_ino == nested.st_ino);
        redo = reversed(undo);
      }
    }
    drain();
    noStages(backend);
    TEST_CHECK(read(backend / "tree/nested/replaced") == "previous content");
  }
  void mergeNoops(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "tree");
    fs::create_directories(backend / "tree");
    write(backend / "tree/keep", "unchanged");
    for (unsigned variant = 0; variant < 2; ++variant) {
      if (variant) {
        write(local / "tree/skip", "skip source");
        write(backend / "tree/skip", "keep destination");
      }
      const auto done = upload({Location::fromPath((local / "tree").native())}, remote, mergeChoice);
      TEST_CHECK(!done.receipt && done.outcomes.front().outcome == Copied && !done.outcomes.front().error);
      TEST_CHECK(read(backend / "tree/keep") == "unchanged");
      drain();
      noStages(backend);
    }
    TEST_CHECK(read(backend / "tree/skip") == "keep destination");
  }
  void mergeParentRefusal(const fs::path& local, const Location& remote, const fs::path& backend) {
    // Every merged ancestor must remain a real directory, including an ancestor above the immediate parent.
    for (unsigned variant = 0; variant < 2; ++variant) {
      const auto name = std::to_string(variant);
      const auto source = local / name / "tree";
      const auto destination = backend / name;
      fs::create_directories(source / "nested");
      fs::create_directories(destination / "tree/nested");
      write(source / "a", "first copied item");
      write(source / "nested/z", "last copied item");
      {
        auto done = upload({Location::fromPath(source.native())}, remote.child(name), mergeChoice);
        TEST_CHECK(done.receipt);
        const auto original = destination / (variant ? "tree" : "tree/nested");
        const auto retired = destination / "retired";
        fs::rename(original, retired);
        fs::create_directory_symlink(retired, original);
        auto blocked = replay(done.receipt);
        TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
        TEST_CHECK(read(destination / "tree/a") == "first copied item");
        TEST_CHECK(read(destination / "tree/nested/z") == "last copied item");
        fs::remove(original);
        fs::rename(retired, original);
      }
      drain();
      noStages(destination);
    }
  }
  void mergePreflight(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "tree/nested");
    fs::create_directories(backend / "tree/nested");
    write(local / "tree/a", "incoming a");
    write(local / "tree/nested/z", "incoming z");
    const auto source = Location::fromPath((local / "tree").native());
    {
      auto done = upload({source}, remote, mergeChoice);
      TEST_CHECK(done.receipt);
      CancelToken cancelled;
      cancelled.cancel();
      std::atomic<std::size_t> count{0};
      auto stopped = done.receipt->reverse(cancelled, count);
      TEST_CHECK(!stopped.receipt && !stopped.retireHistory);
      TEST_CHECK(read(backend / "tree/a") == "incoming a" && read(backend / "tree/nested/z") == "incoming z");
      const auto stamp = fs::last_write_time(backend / "tree/nested/z");
      write(backend / "tree/nested/z", "external z");
      fs::last_write_time(backend / "tree/nested/z", stamp);
      const auto blocked = replay(done.receipt);
      TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
      TEST_CHECK(read(backend / "tree/a") == "incoming a");
    }
    drain();
    noStages(backend);
    {
      auto done = upload({source}, remote, mergeChoice);
      TEST_CHECK(done.receipt);
      auto redo = reversed(done.receipt);
      write(backend / "tree/nested/z", "unknown occupant");
      auto blocked = replay(redo);
      TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
      TEST_CHECK(read(backend / "tree/a") == "incoming a");
      TEST_CHECK(read(backend / "tree/nested/z") == "unknown occupant");
    }
    drain(true);
    for (const auto& directory : {backend / "tree", backend / "tree/nested"}) {
      const auto retained = anchor(directory);
      TEST_CHECK(read(retained / "item") == (directory == backend / "tree" ? "incoming a" : "incoming z"));
      fs::remove_all(retained);
    }
    noStages(backend);
  }
  void mergeCaptureFailure(const fs::path& local, const Location& remote, const fs::path& backend) {
    // A dependent destination or later failure must not delete earlier displaced data during the forward job.
    for (unsigned variant = 0; variant < 2; ++variant) {
      const auto name = std::to_string(variant);
      const auto first = local / name / "first/tree", second = local / name / "second/tree";
      const auto destination = backend / name / "tree";
      fs::create_directories(first);
      fs::create_directories(second);
      fs::create_directories(destination);
      write(first / "a", "first incoming");
      write(second / "a", "second incoming");
      write(destination / "a", "original destination");
      std::vector<Location> sources{Location::fromPath(first.native())};
      if (!variant) {
        sources.push_back(Location::fromPath(second.native()));
      } else {
        TEST_CHECK(::mkfifo((first / "z").c_str(), 0600) == 0);
      }
      {
        const auto done = upload(sources, remote.child(name), mergeChoice);
        TEST_CHECK(!done.receipt);
        TEST_CHECK(done.outcomes.front().outcome == (variant ? Partial : Copied));
        TEST_CHECK(read(destination / "a") == (variant ? "first incoming" : "second incoming"));
        TEST_CHECK(read(first / "a") == "first incoming" && read(second / "a") == "second incoming");
      }
      drain(true);
      std::vector<fs::path> retained;
      std::vector<std::string> contents;
      for (const auto& entry : fs::directory_iterator(destination)) {
        if (entry.path().filename().native().starts_with(".luciole-history-")) {
          retained.push_back(entry.path());
          contents.push_back(read(entry.path() / "previous"));
        }
      }
      TEST_CHECK(contents.size() == (variant ? 1 : 2));
      TEST_CHECK(std::ranges::find(contents, "original destination") != contents.end());
      if (!variant) {
        TEST_CHECK(std::ranges::find(contents, "first incoming") != contents.end());
      }
      for (const auto& path : retained) {
        fs::remove_all(path);
      }
      noStages(destination);
    }
  }
  void mergeEffectLimit(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "tree");
    fs::create_directories(backend / "tree");
    for (unsigned i = 0; i < 129; ++i) {
      write(local / "tree" / ("item-" + std::to_string(i)), "bounded merge");
    }
    {
      auto done = upload({Location::fromPath((local / "tree").native())}, remote, mergeChoice);
      TEST_CHECK(!done.receipt && done.outcomes.front().outcome == Copied);
    }
    drain();
    noStages(backend);
    for (unsigned i = 0; i < 129; ++i) {
      TEST_CHECK(read(backend / "tree" / ("item-" + std::to_string(i))) == "bounded merge");
    }
  }
  void markerRefusal(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "item", "visible content");
    write(local / "outside", "outside canary");
    fs::path retained;
    {
      auto done = upload({Location::fromPath((local / "item").native())}, remote);
      TEST_CHECK(done.receipt);
      retained = anchor(backend);
      fs::create_symlink(local / "outside", retained / "unknown");
      const auto reply = replay(done.receipt);
      TEST_CHECK(!reply.receipt && reply.outcomes.front().error->kind == FileErrorKind::Changed);
      TEST_CHECK(read(backend / "item") == "visible content");
    }
    drain(true);
    TEST_CHECK(read(local / "outside") == "outside canary" && fs::is_symlink(retained / "unknown"));
    fs::remove_all(retained);
  }
  void effectLimit(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    std::vector<Location> sources;
    for (unsigned i = 0; i < 129; ++i) {
      const auto path = local / ("item-" + std::to_string(i));
      write(path, "bounded bytes");
      sources.push_back(Location::fromPath(path.native()));
    }
    {
      const auto result = upload(sources, remote);
      TEST_CHECK(!result.receipt && result.outcomes.size() == sources.size());
      for (const auto& value : result.outcomes) {
        TEST_CHECK(value.outcome == Copied && !value.error);
      }
    }
    drain();
    noStages(backend);
    for (unsigned i = 0; i < 129; ++i) {
      TEST_CHECK(read(backend / ("item-" + std::to_string(i))) == "bounded bytes");
    }
  }
  void unmount(const Location& remote) {
    GError* error = nullptr;
    const GioPtr<GMount> mounted(g_file_find_enclosing_mount(gioFile(remote).get(), nullptr, &error));
    TEST_CHECK(mounted && !error);
    std::optional<bool> done;
    g_mount_unmount_with_operation(
        mounted.get(), G_MOUNT_UNMOUNT_FORCE, nullptr, nullptr,
        [](GObject* object, GAsyncResult* reply, gpointer data) {
          *static_cast<std::optional<bool>*>(data) =
              g_mount_unmount_with_operation_finish(G_MOUNT(object), reply, nullptr);
        },
        &done
    );
    const auto deadline = g_get_monotonic_time() + 5000000;
    while (!done) {
      while (g_main_context_iteration(nullptr, false)) {
      }
      TEST_CHECK(g_get_monotonic_time() < deadline);
      g_usleep(1000);
    }
    TEST_CHECK(*done);
  }
  void disconnected(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "item", "reconnect content");
    {
      auto done = upload({Location::fromPath((local / "item").native())}, remote);
      TEST_CHECK(done.receipt);
      unmount(remote);
      TransferService service;
      TEST_CHECK(service.replayCopy(done.receipt));
      wait(service);
      TEST_CHECK(!service.copyReceipt() && !service.historyRetired());
      TEST_CHECK(service.results().front().error && read(backend / "item") == "reconnect content");
      mount(remote);
      auto redo = reversed(done.receipt);
      auto undo = reversed(redo);
    }
    drain();
    noStages(backend);
  }

  void lostReply(
      const fs::path& local, const Location& remote, const fs::path& backend, const fs::path& arm,
      bool replacement = false, bool restore = false
  ) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "item", "survives lost rename reply");
    if (replacement) {
      write(backend / "item", "previous destination");
    }
    fs::path retained;
    {
      auto done = upload({Location::fromPath((local / "item").native())}, remote, [](const auto&) { return Replace; });
      TEST_CHECK(done.receipt);
      retained = anchor(backend);
      const auto temporary = fs::path(arm.string() + ".tmp");
      write(temporary, (restore ? retained / "previous" : backend / "item").native());
      fs::rename(temporary, arm);
      TransferService service;
      TEST_CHECK(service.replayCopy(done.receipt));
      wait(service);
      TEST_CHECK(service.historyRetired() && !service.copyReceipt());
      TEST_CHECK(service.results().front().outcome == Partial && service.results().front().error);
      TEST_CHECK(read(retained / "item") == "survives lost rename reply");
      if (restore) {
        TEST_CHECK(read(backend / "item") == "previous destination");
      } else {
        TEST_CHECK(!fs::exists(backend / "item"));
        if (replacement) {
          TEST_CHECK(read(retained / "previous") == "previous destination");
        }
      }
      TEST_CHECK(service.report().contains("Inspect") && service.report().contains(retained.filename().native()));
      fs::remove(arm);
    }
    drain(true);
    TEST_CHECK(read(retained / "item") == "survives lost rename reply");
    TEST_CHECK(read(local / "item") == "survives lost rename reply");
    fs::remove_all(retained);
  }

  void lostMergeReply(const fs::path& local, const Location& remote, const fs::path& backend, const fs::path& arm) {
    fs::create_directories(local / "tree");
    fs::create_directories(backend / "tree");
    write(local / "tree/a", "retained merge data");
    write(local / "tree/z", "later merge data");
    write(backend / "tree/keep", "unrelated destination");
    {
      auto done = upload({Location::fromPath((local / "tree").native())}, remote, mergeChoice);
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      const auto temporary = fs::path(arm.string() + ".tmp");
      write(temporary, (backend / "tree/a").native());
      fs::rename(temporary, arm);
      TransferService service;
      TEST_CHECK(service.replayCopy(done.receipt));
      wait(service);
      TEST_CHECK(service.historyRetired() && !service.copyReceipt());
      TEST_CHECK(service.results().front().outcome == Partial && service.results().front().error);
      TEST_CHECK(service.report().contains("Inspect"));
      TEST_CHECK(!fs::exists(backend / "tree/a"));
      TEST_CHECK(read(backend / "tree/z") == "later merge data");
      TEST_CHECK(read(backend / "tree/keep") == "unrelated destination");
      TEST_CHECK(read(local / "tree/a") == "retained merge data");
      bool found = false;
      for (const auto& entry : fs::directory_iterator(backend / "tree")) {
        if (entry.path().filename().native().starts_with(".luciole-history-")) {
          found |= read(entry.path() / "item") == "retained merge data";
        }
      }
      TEST_CHECK(found);
    }
    drain(true);
  }
  fs::path localAnchor(const std::string& name) {
    const char* directory = g_getenv("LUCIOLE_HISTORY_TEST_TMP");
    TEST_CHECK(directory && fs::path(directory) == fs::path(g_get_tmp_dir()));
    for (const auto& entry : fs::directory_iterator(directory)) {
      if (entry.path().filename().native().starts_with(".luciole-history-")
          && fs::symlink_status(entry.path() / "item" / name).type() != fs::file_type::not_found) {
        return entry.path();
      }
    }
    TEST_CHECK(false);
    return {};
  }
  void localHistoryGuards(const fs::path& local, const fs::path& backend) {
    for (unsigned variant = 0; variant < 4; ++variant) {
      const auto name = "guard-" + std::to_string(variant);
      const auto stage = local / name, target = backend / name;
      fs::create_directories(stage);
      fs::create_directories(backend);
      write(stage / "file", "retained guard bytes");
      fs::path retained;
      {
        provider_history::Budget budget;
        auto effect = ProviderCopyEffect::prepareLocal(
            Location::fromPath(target.native()), Location::fromPath(stage.native()), budget, {}
        );
        fs::rename(stage, target);
        effect->published({});
        auto receipt = ProviderCopyReceipt::capture({effect}, {});
        effect.reset();
        TEST_CHECK(receipt);
        auto redo = reversed(receipt);
        retained = localAnchor(name);
        if (variant == 0) {
          fs::rename(retained / "item", retained / "held");
          fs::create_directory_symlink(retained / "held", retained / "item");
        } else if (variant == 1) {
          write(retained / "item/unknown", "unrecorded contents");
        } else if (variant == 2) {
          TEST_CHECK(::chmod(retained.c_str(), 0755) == 0);
        } else {
          write(retained / "history-token", "changed marker");
        }
        const auto blocked = replay(redo);
        TEST_CHECK(!blocked.receipt && blocked.outcomes.front().error->kind == FileErrorKind::Changed);
        TEST_CHECK(!fs::exists(target));
        TEST_CHECK(read(retained / "item" / name / "file") == "retained guard bytes");
      }
      drain(true);
      TEST_CHECK(read(retained / "item" / name / "file") == "retained guard bytes");
      fs::remove_all(retained);
    }
    const auto target = backend / "cleanup-pair", stage = local / "cleanup-pair", old = local / "old";
    write(stage, "incoming cleanup bytes");
    write(target, "previous cleanup bytes");
    fs::path retained;
    {
      provider_history::Budget budget;
      auto effect = ProviderCopyEffect::prepareLocal(
          Location::fromPath(target.native()), Location::fromPath(stage.native()), budget, {}, true
      );
      fs::rename(target, old);
      fs::rename(stage, target);
      effect->published({}, Location::fromPath(old.native()));
      auto receipt = ProviderCopyReceipt::capture({effect}, {});
      TEST_CHECK(receipt);
      fs::remove(old);
      retained = localAnchor("cleanup-pair");
      write(retained / "previous/cleanup-pair", "modified cleanup bytes");
    }
    drain(true);
    TEST_CHECK(read(retained / "item/cleanup-pair") == "incoming cleanup bytes");
    TEST_CHECK(read(retained / "previous/cleanup-pair") == "modified cleanup bytes");
    TEST_CHECK(read(target) == "incoming cleanup bytes");
    fs::remove_all(retained);
  }
  void portableCopies(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "tree/nested");
    fs::create_directories(local / "tree/empty");
    fs::create_directories(backend);
    write(local / "tree/nested/café space.txt", "portable tree bytes");
    write(local / "tree/.hidden", "portable hidden bytes");
    write(local / "file.txt", "portable file bytes");
    {
      auto done = upload(
          {Location::fromPath((local / "tree").native()), Location::fromPath((local / "file.txt").native())}, remote
      );
      for (const auto& result : done.outcomes) {
        if (result.error) {
          std::println(stderr, "portable copy: {}", result.error->message);
        }
        TEST_CHECK(result.outcome == Copied && !result.error);
      }
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      fs::remove_all(local);
      auto redo = reversed(done.receipt);
      TEST_CHECK(!fs::exists(backend / "tree") && !fs::exists(backend / "file.txt"));
      for (unsigned turn = 0; turn < 3; ++turn) {
        auto undo = reversed(redo);
        TEST_CHECK(read(backend / "tree/nested/café space.txt") == "portable tree bytes");
        TEST_CHECK(read(backend / "tree/.hidden") == "portable hidden bytes");
        TEST_CHECK(read(backend / "file.txt") == "portable file bytes" && fs::is_empty(backend / "tree/empty"));
        redo = reversed(undo);
      }
    }
    drain();
    noStages(backend);
  }
  void portableReplacements(const fs::path& local, const Location& remote, const fs::path& backend) {
    for (unsigned variant = 0; variant < 4; ++variant) {
      const auto name = std::to_string(variant);
      const auto source = local / name / "item", target = backend / name / "item";
      fs::create_directories(source.parent_path());
      fs::create_directories(target.parent_path());
      const bool incomingFolder = variant == 1 || variant == 3;
      const bool previousFolder = variant == 2 || variant == 3;
      if (incomingFolder) {
        fs::create_directory(source);
        write(source / "new", "incoming folder");
      } else {
        write(source, "incoming file");
      }
      if (previousFolder) {
        fs::create_directory(target);
        write(target / "old", "previous folder");
      } else {
        write(target, "previous file");
      }
      {
        auto done =
            upload({Location::fromPath(source.native())}, remote.child(name), [](const auto&) { return Replace; });
        TEST_CHECK(done.receipt && done.outcomes.front().outcome == Copied);
        fs::remove_all(source);
        auto redo = reversed(done.receipt);
        for (unsigned turn = 0; turn < 2; ++turn) {
          TEST_CHECK(
              read(previousFolder ? target / "old" : target) == (previousFolder ? "previous folder" : "previous file")
          );
          auto undo = reversed(redo);
          TEST_CHECK(
              read(incomingFolder ? target / "new" : target) == (incomingFolder ? "incoming folder" : "incoming file")
          );
          redo = reversed(undo);
        }
      }
      drain();
      noStages(target.parent_path());
    }
  }
  void portableMerges(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local / "tree/nested");
    fs::create_directories(local / "tree/empty");
    fs::create_directories(backend / "tree/nested");
    write(local / "tree/new", "new merge bytes");
    write(local / "tree/nested/replace", "incoming merge bytes");
    write(local / "tree/nested/numbered", "numbered merge bytes");
    write(local / "tree/nested/skip", "skipped source");
    write(backend / "tree/nested/replace", "previous merge bytes");
    write(backend / "tree/nested/numbered", "keep numbered");
    write(backend / "tree/nested/skip", "keep skipped");
    write(backend / "tree/keep", "unrelated merge bytes");
    struct stat before{};
    TEST_CHECK(::lstat((backend / "tree").c_str(), &before) == 0);
    {
      auto done = upload({Location::fromPath((local / "tree").native())}, remote, mergeChoice);
      TEST_CHECK(done.receipt && done.receipt->size() == 4 && done.outcomes.front().outcome == Copied);
      fs::remove_all(local);
      write(backend / "tree/nested/later", "later sibling");
      auto redo = reversed(done.receipt);
      for (unsigned turn = 0; turn < 2; ++turn) {
        TEST_CHECK(!fs::exists(backend / "tree/new") && !fs::exists(backend / "tree/empty"));
        TEST_CHECK(read(backend / "tree/nested/replace") == "previous merge bytes");
        TEST_CHECK(!fs::exists(backend / "tree/nested/numbered (1)"));
        auto undo = reversed(redo);
        TEST_CHECK(read(backend / "tree/new") == "new merge bytes" && fs::is_empty(backend / "tree/empty"));
        TEST_CHECK(read(backend / "tree/nested/replace") == "incoming merge bytes");
        TEST_CHECK(read(backend / "tree/nested/numbered (1)") == "numbered merge bytes");
        TEST_CHECK(read(backend / "tree/nested/numbered") == "keep numbered");
        TEST_CHECK(read(backend / "tree/nested/skip") == "keep skipped");
        TEST_CHECK(read(backend / "tree/nested/later") == "later sibling");
        TEST_CHECK(read(backend / "tree/keep") == "unrelated merge bytes");
        struct stat after{};
        TEST_CHECK(::lstat((backend / "tree").c_str(), &after) == 0 && after.st_ino == before.st_ino);
        redo = reversed(undo);
      }
    }
    drain();
    noStages(backend);
  }
  void portableAliases(const fs::path& local, const Location& remote, const fs::path& backend) {
    fs::create_directories(local);
    fs::create_directories(backend);
    write(local / "a", "same incoming bytes");
    write(local / "A", "same incoming bytes");
    fs::last_write_time(local / "A", fs::last_write_time(local / "a"));
    write(backend / "a", "original alias bytes");
    {
      auto done = upload(
          {Location::fromPath((local / "a").native()), Location::fromPath((local / "A").native())}, remote,
          [](const auto&) { return Replace; }
      );
      TEST_CHECK(!done.receipt && done.outcomes.size() == 2);
      TEST_CHECK(done.outcomes[0].outcome == Copied && done.outcomes[1].outcome == Copied);
      TEST_CHECK(read(backend / "A") == "same incoming bytes");
    }
    drain(true);
    const auto first = localAnchor("a"), second = localAnchor("A");
    TEST_CHECK(read(first / "previous/a") == "original alias bytes");
    TEST_CHECK(read(second / "previous/A") == "same incoming bytes");
    fs::remove_all(first);
    fs::remove_all(second);
    noStages(backend);
  }
  void portableHistory(const fs::path& local, const Location& remote, const fs::path& backend) {
    portableCopies(local / "copy", remote.child("copy"), backend / "copy");
    portableReplacements(local / "replace", remote.child("replace"), backend / "replace");
    portableMerges(local / "merge", remote.child("merge"), backend / "merge");
    collisionsAndEdits(local / "edits", remote.child("edits"), backend / "edits");
    cancellationAndDiscard(local / "cancel", remote.child("cancel"), backend / "cancel");
    budget(local / "budget", remote.child("budget"), backend / "budget");
    fullSelection(local / "selection", remote.child("selection"), backend / "selection");
    mergeNoops(local / "noops", remote.child("noops"), backend / "noops");
    disconnected(local / "disconnect", remote.child("disconnect"), backend / "disconnect");
    if (remote.uri().starts_with("smb:")) {
      portableAliases(local / "aliases", remote.child("aliases"), backend / "aliases");
    }
  }
  void localPartialCancellation(const fs::path& local, const fs::path& backend) {
    const auto stage = local / "stage", target = backend / "bulk";
    fs::create_directories(stage);
    fs::create_directories(backend);
    write(stage / "first", "first retained bytes");
    for (unsigned i = 0; i < 512; ++i) {
      write(stage / ("item-" + std::to_string(i)), "retained cancellation bytes");
    }
    fs::path retained;
    bool preserve = false;
    {
      provider_history::Budget budget;
      auto effect = ProviderCopyEffect::prepareLocal(
          Location::fromPath(target.native()), Location::fromPath(stage.native()), budget, {}
      );
      fs::rename(stage, target);
      effect->published({});
      auto receipt = ProviderCopyReceipt::capture({effect}, {});
      effect.reset();
      TEST_CHECK(receipt);
      retained = localAnchor("bulk");
      CancelToken cancel;
      std::atomic<bool> finished{false};
      std::jthread cancellation([&] {
        while (!finished.load() && fs::exists(target / "first")) {
          g_usleep(100);
        }
        if (!finished.load()) {
          cancel.cancel();
        }
      });
      std::atomic<std::size_t> completed{0};
      auto result = receipt->reverse(cancel, completed);
      finished.store(true);
      cancellation.join();
      TEST_CHECK(completed.load() == 1);
      if (result.receipt) {
        TEST_CHECK(!fs::exists(target));
      } else {
        TEST_CHECK(result.retireHistory && result.results.front().outcome == Partial);
        preserve = true;
      }
      TEST_CHECK(!fs::exists(target / "first"));
      TEST_CHECK(read(retained / "item/bulk/first") == "first retained bytes");
      for (unsigned i = 0; i < 512; ++i) {
        const auto name = "item-" + std::to_string(i);
        TEST_CHECK(read(retained / "item/bulk" / name) == "retained cancellation bytes");
        TEST_CHECK(!fs::exists(target / name) || read(target / name) == "retained cancellation bytes");
      }
    }
    drain(preserve);
    if (preserve) {
      TEST_CHECK(read(retained / "item/bulk/first") == "first retained bytes");
      fs::remove_all(retained);
    }
  }
  void lostPortableReply(const fs::path& local, const Location& remote, const fs::path& backend, const fs::path& arm) {
    fs::create_directories(local / "tree");
    fs::create_directories(backend / "tree");
    write(local / "tree/a", "incoming uncertain bytes");
    write(local / "tree/z", "later unchanged bytes");
    write(backend / "tree/a", "previous uncertain bytes");
    write(backend / "tree/keep", "unrelated destination");
    {
      auto done = upload({Location::fromPath((local / "tree").native())}, remote, mergeChoice);
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      const auto temporary = fs::path(arm.string() + ".tmp");
      write(temporary, (backend / "tree/a").native());
      fs::rename(temporary, arm);
      TransferService service;
      TEST_CHECK(service.replayCopy(done.receipt));
      wait(service);
      TEST_CHECK(service.historyRetired() && !service.copyReceipt());
      TEST_CHECK(service.results().front().outcome == Partial && service.results().front().error);
      TEST_CHECK(service.report().contains("Inspect"));
      TEST_CHECK(!fs::exists(backend / "tree/a"));
      TEST_CHECK(read(backend / "tree/z") == "later unchanged bytes");
      TEST_CHECK(read(backend / "tree/keep") == "unrelated destination");
      const auto retained = localAnchor("a");
      TEST_CHECK(read(retained / "item/a") == "incoming uncertain bytes");
      TEST_CHECK(read(retained / "previous/a") == "previous uncertain bytes");
      TEST_CHECK(read(local / "tree/a") == "incoming uncertain bytes");
    }
    drain(true);
    const auto retained = localAnchor("a");
    TEST_CHECK(read(retained / "item/a") == "incoming uncertain bytes");
    TEST_CHECK(read(retained / "previous/a") == "previous uncertain bytes");
  }
  void run(const fs::path& local, const Location& remote, const fs::path& backend) {
    basic(local / "basic", remote.child("basic"), backend / "basic");
    collisionsAndEdits(local / "collisions", remote.child("collisions"), backend / "collisions");
    replacements(local / "replacements", remote.child("replacements"), backend / "replacements");
    replacementRefusal(local / "replace-refusal", remote.child("replace-refusal"), backend / "replace-refusal");
    replacementSelection(local / "replace-selection", remote.child("replace-selection"), backend / "replace-selection");
    cancellationAndDiscard(local / "cancel", remote.child("cancel"), backend / "cancel");
    cleanupRefusal(local / "refusal", remote.child("refusal"), backend / "refusal");
    budget(local / "budget", remote.child("budget"), backend / "budget");
    fullSelection(local / "selection", remote.child("selection"), backend / "selection");
    merges(local / "merges", remote.child("merges"), backend / "merges");
    mergeNoops(local / "merge-noops", remote.child("merge-noops"), backend / "merge-noops");
    mergeParentRefusal(local / "merge-parents", remote.child("merge-parents"), backend / "merge-parents");
    mergePreflight(local / "merge-preflight", remote.child("merge-preflight"), backend / "merge-preflight");
    mergeCaptureFailure(local / "merge-capture", remote.child("merge-capture"), backend / "merge-capture");
    markerRefusal(local / "marker", remote.child("marker"), backend / "marker");
    if (remote.isNative()) {
      effectLimit(local / "limit", remote.child("limit"), backend / "limit");
      mergeEffectLimit(local / "merge-limit", remote.child("merge-limit"), backend / "merge-limit");
    } else {
      disconnected(local / "disconnected", remote.child("disconnected"), backend / "disconnected");
    }
  }
} // namespace

int main(int argc, char* argv[]) {
  GlibPollSource glib;
  if (argc == 6 && std::string_view(argv[1]) == "--lost-portable") {
    const auto remote = Location::fromUri(argv[2]);
    TEST_CHECK(!remote.isNative() && transferLocationSupported(remote));
    mount(remote);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("lost-") + uuid.get();
    lostPortableReply(fs::path(argv[4]) / name, remote.child(name), fs::path(argv[3]) / name, argv[5]);
  } else if (argc == 7 && std::string_view(argv[1]) == "--portable-provider") {
    const auto remote = Location::fromUri(argv[2]), readOnly = Location::fromUri(argv[5]);
    TEST_CHECK(!remote.isNative() && transferLocationSupported(remote) && transferLocationSupported(readOnly));
    mount(remote);
    mount(readOnly);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("history-") + uuid.get();
    const auto backend = fs::path(argv[3]) / name, local = fs::path(argv[4]) / name;
    fs::create_directories(backend);
    fs::create_directories(local);
    portableHistory(local, remote.child(name), backend);
    write(local / "denied", "read-only source");
    const auto denied = upload({Location::fromPath((local / "denied").native())}, readOnly);
    TEST_CHECK(!denied.receipt && denied.outcomes.front().outcome == Failed);
    TEST_CHECK(read(local / "denied") == "read-only source" && !fs::exists(fs::path(argv[6]) / "denied"));
    drain();
  } else if (
      argc == 6
      && (std::string_view(argv[1]) == "--lost-reply"
          || std::string_view(argv[1]) == "--lost-replacement"
          || std::string_view(argv[1]) == "--lost-restore"
          || std::string_view(argv[1]) == "--lost-merge"
          || std::string_view(argv[1]) == "--lost-link")
  ) {
    const auto remote = Location::fromUri(argv[2]);
    TEST_CHECK(remote.uri().starts_with("sftp:"));
    mount(remote);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("lost-reply-") + uuid.get();
    const auto backend = fs::path(argv[3]) / name, local = fs::path(argv[4]) / name;
    if (std::string_view(argv[1]) == "--lost-link") {
      lostLinkReply(remote.child(name), backend, argv[5]);
    } else if (std::string_view(argv[1]) == "--lost-merge") {
      lostMergeReply(local, remote.child(name), backend, argv[5]);
    } else {
      lostReply(
          local, remote.child(name), backend, argv[5], std::string_view(argv[1]) != "--lost-reply",
          std::string_view(argv[1]) == "--lost-restore"
      );
    }
  } else if (
      argc == 5
      && (std::string_view(argv[1]) == "--provider"
          || std::string_view(argv[1]) == "--links"
          || std::string_view(argv[1]) == "--denied-links")
  ) {
    const auto remote = Location::fromUri(argv[2]);
    TEST_CHECK(remote.uri().starts_with("sftp:"));
    mount(remote);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("history-") + uuid.get();
    const auto backend = fs::path(argv[3]) / name, local = fs::path(argv[4]) / name;
    fs::create_directories(backend);
    fs::create_directories(local);
    if (std::string_view(argv[1]) == "--denied-links") {
      fs::create_directories(backend / "source");
      fs::create_directories(backend / "destination");
      write(backend / "source/file", "unchanged source");
      write(backend / "destination/file", "unchanged destination");
      auto refused = upload(
          {remote.child(name).child("source").child("file")}, remote.child(name).child("destination"),
          [](const auto&) { return Replace; }, TransferAction::Link
      );
      TEST_CHECK(!refused.receipt && refused.outcomes.front().outcome == Failed);
      TEST_CHECK(refused.outcomes.front().error);
      TEST_CHECK(refused.outcomes.front().error->message.contains("Could not create symbolic link"));
      TEST_CHECK(read(backend / "source/file") == "unchanged source");
      TEST_CHECK(read(backend / "destination/file") == "unchanged destination");
      drain();
      noStages(backend);
    } else if (std::string_view(argv[1]) == "--links") {
      linkHistory(remote.child(name), backend);
    } else {
      run(local, remote.child(name), backend);
    }
  } else {
    TEST_CHECK(argc == 1);
    const luciole::GString temp(g_dir_make_tmp("luciole-provider-history-XXXXXX", nullptr));
    TEST_CHECK(temp);
    const fs::path root(temp.get());
    fs::create_directories(root / "backend");
    run(root / "local", Location::fromPath((root / "backend").native()), root / "backend");
    linkNamespaces();
    linkHistory(Location::fromPath((root / "links").native()), root / "links");
    localRetention(root / "retention-local", root / "retention-backend");
    localHistoryGuards(root / "guard-local", root / "guard-backend");
    localPartialCancellation(root / "partial-local", root / "partial-backend");
    fs::remove_all(root);
  }
  std::println("Provider copy history checks passed");
}
