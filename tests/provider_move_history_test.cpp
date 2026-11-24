#include "event_loop/glib_poll_source.h"
#include "fs/gio_interop.h"
#include "operations/copy_receipt.h"
#include "operations/move_receipt.h"
#include "operations/provider_transfer.h"
#include "test_check.h"

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <print>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  using namespace luciole;
  namespace fs = std::filesystem;
  using enum TransferOutcome;
  using enum TransferConflictChoice;
  void write(const fs::path& path, const std::string& data) { std::ofstream(path) << data; }
  std::string read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
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
    TEST_CHECK(messages.empty() != errors);
  }
  struct Result {
    std::vector<TransferResult> outcomes;
    std::shared_ptr<const MoveReceipt> receipt;
    bool retired = false;
  };
  Result move(
      const std::vector<Location>& sources, const Location& target,
      const ProviderTransfer::Choose& choose = [](const auto&) { return KeepBoth; }
  ) {
    const TransferRequest request{sources, target, TransferAction::Move};
    if (target.isNative()) {
      ProviderTransfer provider(request);
      std::atomic<std::uint64_t> bytes{0};
      std::atomic<std::size_t> completed{0};
      auto results = provider.run({}, bytes, completed, choose, nullptr, true);
      return {std::move(results), provider.takeMoveReceipt({})};
    }
    TransferService service;
    TEST_CHECK(service.start(request));
    wait(service, choose);
    return {service.results(), service.takeReceipt()};
  }
  Result replay(const std::shared_ptr<const MoveReceipt>& receipt) {
    TEST_CHECK(receipt);
    TransferService service;
    TEST_CHECK(service.replay(receipt));
    wait(service);
    return {service.results(), service.takeReceipt(), service.historyRetired()};
  }
  std::shared_ptr<const MoveReceipt> reversed(const std::shared_ptr<const MoveReceipt>& receipt) {
    auto result = replay(receipt);
    for (const auto& outcome : result.outcomes) {
      if (outcome.error) {
        std::println(stderr, "replay: {}", outcome.error->message);
      }
      TEST_CHECK(outcome.outcome == Moved && !outcome.error);
    }
    TEST_CHECK(result.receipt && !result.retired);
    return result.receipt;
  }
  void noStages(const fs::path& root) {
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      TEST_CHECK(!entry.path().filename().native().starts_with(".luciole-"));
    }
  }
  fs::path history(const fs::path& parent) {
    for (const auto& item : fs::directory_iterator(parent)) {
      if (item.path().filename().native().starts_with(".luciole-history-")) {
        return item.path();
      }
    }
    TEST_CHECK(false);
    return {};
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
  void basic(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/tree/nested");
    fs::create_directories(backend / "destination");
    const auto source = remote.child("source"), target = remote.child("destination");
    const std::string raw = std::string("raw-") + static_cast<char>(0xff);
    write(backend / "source/tree/nested/café %.txt", "nested contents");
    write(backend / "source/tree/.hidden", "hidden contents");
    fs::create_directory(backend / "source/tree/empty");
    fs::create_symlink("missing", backend / "source/tree/dangling");
    write(backend / "source" / raw, "raw bytes");
    {
      auto done = move({source.child("tree"), source.child(raw)}, target);
      for (const auto& item : done.outcomes) {
        if (item.error) {
          std::println(stderr, "move: {}", item.error->message);
        }
        TEST_CHECK(item.outcome == Moved && !item.error);
      }
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      TEST_CHECK(!fs::exists(backend / "source/tree") && !fs::exists(backend / "source" / raw));
      TEST_CHECK(read(backend / "destination/tree/nested/café %.txt") == "nested contents");
      write(backend / "source/later", "unrelated source sibling");
      write(backend / "destination/later", "unrelated destination sibling");
      auto redo = reversed(done.receipt);
      TEST_CHECK(read(backend / "source/tree/nested/café %.txt") == "nested contents");
      TEST_CHECK(fs::read_symlink(backend / "source/tree/dangling") == "missing");
      TEST_CHECK(!fs::exists(backend / "destination/tree"));
      auto stale = replay(done.receipt);
      TEST_CHECK(!stale.receipt && stale.retired);
      auto undo = reversed(redo);
      redo = reversed(undo);
      undo = reversed(redo);
      TEST_CHECK(read(backend / "destination" / raw) == "raw bytes");
      TEST_CHECK(read(backend / "source/later") == "unrelated source sibling");
      TEST_CHECK(read(backend / "destination/later") == "unrelated destination sibling");
    }
    drain();
    noStages(backend);
  }
  void replacements(const Location& remote, const fs::path& backend) {
    for (int shape = 0; shape < 3; ++shape) {
      const auto path = backend / std::to_string(shape);
      const auto root = remote.child(std::to_string(shape));
      fs::create_directories(path / "source");
      fs::create_directories(path / "destination");
      if (shape == 0) {
        write(path / "source/item", "incoming");
      } else {
        fs::create_directory(path / "source/item");
        write(path / "source/item/new", "incoming");
      }
      if (shape == 1) {
        write(path / "destination/item", "previous");
      } else {
        fs::create_directory(path / "destination/item");
        write(path / "destination/item/old", "previous");
      }
      {
        auto done =
            move({root.child("source").child("item")}, root.child("destination"), [](const auto&) { return Replace; });
        TEST_CHECK(done.receipt && done.outcomes.front().outcome == Moved);
        const auto retained = history(path / "destination");
        TEST_CHECK(
            shape == 1 ? read(retained / "previous") == "previous" : read(retained / "previous/old") == "previous"
        );
        struct stat mode{};
        TEST_CHECK(::stat(retained.c_str(), &mode) == 0 && (mode.st_mode & 0777U) == 0700);
        auto redo = reversed(done.receipt);
        TEST_CHECK(
            shape == 0 ? read(path / "source/item") == "incoming" : read(path / "source/item/new") == "incoming"
        );
        TEST_CHECK(
            shape == 1 ? read(path / "destination/item") == "previous"
                       : read(path / "destination/item/old") == "previous"
        );
        auto undo = reversed(redo);
        redo = reversed(undo);
      }
      drain();
      noStages(path);
    }
  }
  void collisions(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source");
    fs::create_directories(backend / "destination");
    write(backend / "source/a", "incoming");
    {
      auto done = move({remote.child("source").child("a")}, remote.child("destination"));
      TEST_CHECK(done.receipt);
      CancelToken token;
      token.cancel();
      std::atomic<std::size_t> completed{0};
      auto cancelled = done.receipt->reverse(token, completed);
      TEST_CHECK(!cancelled.retireHistory && cancelled.results.front().outcome == Cancelled);
      TEST_CHECK(!fs::exists(backend / "source/a") && read(backend / "destination/a") == "incoming");
      write(backend / "source/a", "later occupant");
      auto refused = replay(done.receipt);
      TEST_CHECK(!refused.receipt && !refused.retired && refused.outcomes.front().error->kind == FileErrorKind::Exists);
      TEST_CHECK(read(backend / "source/a") == "later occupant");
      fs::remove(backend / "source/a");
      auto redo = reversed(done.receipt);
      write(backend / "destination/a", "redo occupant");
      refused = replay(redo);
      TEST_CHECK(!refused.receipt && !refused.retired && read(backend / "destination/a") == "redo occupant");
      fs::remove(backend / "destination/a");
      auto undo = reversed(redo);
    }
    drain();
    noStages(backend);
  }
  void guards(const Location& remote, const fs::path& backend) {
    for (int variant = 0; variant < 4; ++variant) {
      const auto root = remote.child(std::to_string(variant));
      const auto path = backend / std::to_string(variant);
      fs::create_directories(path / "source");
      fs::create_directories(path / "destination");
      write(path / "source/a", "incoming");
      write(path / "destination/a", "previous");
      {
        auto done =
            move({root.child("source").child("a")}, root.child("destination"), [](const auto&) { return Replace; });
        TEST_CHECK(done.receipt);
        if (variant == 0) {
          struct stat before{};
          TEST_CHECK(::stat((path / "destination/a").c_str(), &before) == 0);
          write(path / "destination/a", "changed!");
          const timespec times[]{before.st_atim, before.st_mtim};
          TEST_CHECK(::utimensat(AT_FDCWD, (path / "destination/a").c_str(), times, 0) == 0);
        } else if (variant == 1) {
          fs::rename(path / "source", path / "held-source");
          fs::create_directory(path / "source");
        } else if (variant == 2) {
          write(history(path / "source") / "unknown", "preserve unknown");
        } else {
          write(history(path / "destination") / "previous", "edited previous");
        }
        const auto retained = history(path / "destination");
        auto refused = replay(done.receipt);
        TEST_CHECK(!refused.receipt && refused.retired && refused.outcomes.front().outcome == Failed);
        TEST_CHECK(!fs::exists(path / "source/a") && fs::exists(path / "destination/a"));
        TEST_CHECK(read(retained / "previous") == (variant == 3 ? "edited previous" : "previous"));
      }
      drain(true);
      TEST_CHECK(fs::exists(history(path / "destination") / "previous"));
      fs::remove_all(path);
    }
  }
  void preflight(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source");
    fs::create_directories(backend / "destination");
    write(backend / "source/a", "first");
    write(backend / "source/b", "second");
    {
      auto done =
          move({remote.child("source").child("a"), remote.child("source").child("b")}, remote.child("destination"));
      TEST_CHECK(done.receipt && done.receipt->size() == 2);
      write(backend / "destination/b", "edited");
      auto refused = replay(done.receipt);
      TEST_CHECK(!refused.receipt && refused.retired && read(backend / "destination/a") == "first");
      TEST_CHECK(!fs::exists(backend / "source/a") && !fs::exists(backend / "source/b"));
    }
    drain(true);
    fs::remove_all(backend);
  }
  void excluded(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/tree");
    fs::create_directories(backend / "destination/tree");
    write(backend / "source/tree/a", "merged");
    write(backend / "destination/tree/keep", "unrelated");
    {
      auto merged =
          move({remote.child("source").child("tree")}, remote.child("destination"), [](const auto&) { return Merge; });
      TEST_CHECK(merged.receipt && merged.outcomes.front().outcome == Moved);
      TEST_CHECK(
          read(backend / "destination/tree/a") == "merged" && read(backend / "destination/tree/keep") == "unrelated"
      );
    }
    drain();
    noStages(backend);
    write(backend / "source/a", "incoming");
    write(backend / "destination/a", "existing");
    auto skip =
        move({remote.child("source").child("a")}, remote.child("destination"), [](const auto&) { return Skip; });
    TEST_CHECK(!skip.receipt && skip.outcomes.front().outcome == Skipped);
    {
      auto numbered = move({remote.child("source").child("a")}, remote.child("destination"));
      TEST_CHECK(
          numbered.receipt && numbered.outcomes.front().destination == remote.child("destination").child("a (1)")
      );
      auto redo = reversed(numbered.receipt);
    }
    drain();
    noStages(backend);
    auto partial =
        move({remote.child("source").child("a"), remote.child("source").child("missing")}, remote.child("destination"));
    TEST_CHECK(
        !partial.receipt && partial.outcomes.front().outcome == Moved && partial.outcomes.back().outcome == Failed
    );
    drain();
    noStages(backend);
  }
  void overlapsAndLimits(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/parent/child");
    fs::create_directories(backend / "destination");
    write(backend / "source/parent/child/file", "overlapping selection");
    auto result = move(
        {remote.child("source").child("parent"), remote.child("source").child("parent").child("child")},
        remote.child("destination")
    );
    TEST_CHECK(!result.receipt && result.outcomes[0].outcome == Moved && result.outcomes[1].outcome == Skipped);
    TEST_CHECK(read(backend / "destination/parent/child/file") == "overlapping selection");
    drain();
    noStages(backend);
    std::vector<Location> sources;
    sources.reserve(129);
    for (unsigned i = 0; i < 129; ++i) {
      const auto name = std::to_string(i);
      write(backend / "source" / name, "small file");
      sources.push_back(remote.child("source").child(name));
    }
    result = move(sources, remote.child("destination"));
    TEST_CHECK(!result.receipt);
    for (const auto& item : result.outcomes) {
      TEST_CHECK(item.outcome == Moved && !item.error);
    }
    drain();
    noStages(backend);
    fs::create_directory(backend / "source/special");
    TEST_CHECK(::mkfifo((backend / "source/special/fifo").c_str(), 0600) == 0);
    write(backend / "destination/special", "existing destination");
    result = move({remote.child("source").child("special")}, remote.child("destination"), [](const auto&) {
      return Replace;
    });
    TEST_CHECK(!result.receipt && result.outcomes.front().outcome == Failed);
    TEST_CHECK(
        fs::is_fifo(backend / "source/special/fifo") && read(backend / "destination/special") == "existing destination"
    );
    drain();
    noStages(backend);
  }
  void lostReply(const Location& remote, const fs::path& backend, const fs::path& arm, unsigned variant) {
    TEST_CHECK(variant < 4);
    fs::create_directories(backend / "source");
    fs::create_directories(backend / "destination");
    write(backend / "source/file", "incoming");
    if (variant != 0) {
      write(backend / "destination/file", "previous");
    }
    {
      auto moved = move({remote.child("source").child("file")}, remote.child("destination"), [](const auto&) {
        return Replace;
      });
      TEST_CHECK(moved.receipt && moved.outcomes.front().outcome == Moved);
      const auto retained = history(backend / "destination");
      auto receipt = variant >= 2 ? reversed(moved.receipt) : moved.receipt;
      const auto trigger = variant == 1 ? retained / "previous"
          : variant == 3                ? backend / "source/file"
                                        : backend / "destination/file";
      write(arm, trigger.native());
      auto uncertain = replay(receipt);
      TEST_CHECK(!uncertain.receipt && uncertain.retired && uncertain.outcomes.front().outcome == Partial);
      TEST_CHECK(uncertain.outcomes.front().error->message.contains("Inspect"));
      if (variant < 2) {
        TEST_CHECK(read(backend / "source/file") == "incoming");
        TEST_CHECK(
            variant == 0 ? !fs::exists(backend / "destination/file") : read(backend / "destination/file") == "previous"
        );
      } else {
        TEST_CHECK(read(retained / "previous") == "previous");
        TEST_CHECK(
            variant == 2 ? read(backend / "source/file") == "incoming"
                         : read(backend / "destination/file") == "incoming"
        );
      }
    }
    drain(true);
    TEST_CHECK(fs::exists(history(backend / "source") / "history-token"));
    TEST_CHECK(fs::exists(history(backend / "destination") / "history-token"));
  }
  TransferConflictChoice mergeChoice(const TransferConflict& conflict) { return conflict.canMerge ? Merge : Replace; }
  void mergeHistory(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/tree/nested");
    fs::create_directories(backend / "source/tree/new-dir/empty");
    fs::create_directories(backend / "destination/tree/nested");
    const std::string raw = std::string("raw-") + static_cast<char>(0xff);
    write(backend / "source/tree/nested/replaced", "incoming");
    write(backend / "source/tree/nested/café % #.txt", "unicode");
    write(backend / "source/tree/new-dir/.hidden", "hidden");
    write(backend / "source/tree" / raw, "raw");
    fs::create_symlink("absent", backend / "source/tree/dangling");
    write(backend / "destination/tree/nested/replaced", "previous");
    write(backend / "destination/tree/keep", "destination only");
    struct stat sourceBefore{}, targetBefore{}, nestedBefore{};
    TEST_CHECK(::stat((backend / "source/tree").c_str(), &sourceBefore) == 0);
    TEST_CHECK(::stat((backend / "destination/tree").c_str(), &targetBefore) == 0);
    TEST_CHECK(::stat((backend / "destination/tree/nested").c_str(), &nestedBefore) == 0);
    {
      auto moved = move({remote.child("source").child("tree")}, remote.child("destination"), mergeChoice);
      if (moved.outcomes.front().error) {
        std::println(stderr, "merge: {}", moved.outcomes.front().error->message);
      }
      TEST_CHECK(moved.receipt && moved.outcomes.front().outcome == Moved);
      TEST_CHECK(!fs::exists(backend / "source/tree"));
      TEST_CHECK(read(backend / "destination/tree/nested/replaced") == "incoming");
      write(backend / "destination/tree/later", "later root sibling");
      write(backend / "destination/tree/nested/later", "later nested sibling");
      auto redo = reversed(moved.receipt);
      TEST_CHECK(read(backend / "source/tree/nested/replaced") == "incoming");
      TEST_CHECK(read(backend / "source/tree/new-dir/.hidden") == "hidden");
      TEST_CHECK(fs::read_symlink(backend / "source/tree/dangling") == "absent");
      TEST_CHECK(read(backend / "source/tree" / raw) == "raw");
      TEST_CHECK(read(backend / "destination/tree/nested/replaced") == "previous");
      TEST_CHECK(!fs::exists(backend / "destination/tree/new-dir"));
      struct stat actual{};
      TEST_CHECK(::stat((backend / "source/tree").c_str(), &actual) == 0 && actual.st_ino == sourceBefore.st_ino);
      auto undo = reversed(redo);
      redo = reversed(undo);
      undo = reversed(redo);
      TEST_CHECK(read(backend / "destination/tree/keep") == "destination only");
      TEST_CHECK(read(backend / "destination/tree/later") == "later root sibling");
      TEST_CHECK(read(backend / "destination/tree/nested/later") == "later nested sibling");
      TEST_CHECK(::stat((backend / "destination/tree").c_str(), &actual) == 0 && actual.st_ino == targetBefore.st_ino);
      TEST_CHECK(
          ::stat((backend / "destination/tree/nested").c_str(), &actual) == 0 && actual.st_ino == nestedBefore.st_ino
      );
    }
    drain();
    noStages(backend);
  }
  void emptyAndMixedMerges(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/empty");
    fs::create_directories(backend / "destination/empty");
    write(backend / "destination/empty/keep", "unrelated");
    {
      auto moved = move({remote.child("source").child("empty")}, remote.child("destination"), mergeChoice);
      TEST_CHECK(moved.receipt && moved.receipt->size() == 1 && !fs::exists(backend / "source/empty"));
      auto redo = reversed(moved.receipt);
      TEST_CHECK(fs::is_empty(backend / "source/empty") && read(backend / "destination/empty/keep") == "unrelated");
      auto undo = reversed(redo);
    }
    drain();
    noStages(backend);
    fs::create_directory(backend / "source/tree");
    fs::create_directory(backend / "destination/tree");
    write(backend / "source/tree/new", "merged");
    write(backend / "source/plain", "incoming whole");
    write(backend / "destination/plain", "previous whole");
    write(backend / "source/skipped", "keep source");
    write(backend / "destination/skipped", "keep target");
    {
      auto moved = move(
          {remote.child("source").child("tree"), remote.child("source").child("plain"),
           remote.child("source").child("skipped")},
          remote.child("destination"),
          [](const auto& conflict) { return conflict.name == "skipped" ? Skip : mergeChoice(conflict); }
      );
      TEST_CHECK(
          moved.receipt
          && moved.outcomes[0].outcome == Moved
          && moved.outcomes[1].outcome == Moved
          && moved.outcomes[2].outcome == Skipped
      );
      auto redo = reversed(moved.receipt);
      TEST_CHECK(read(backend / "source/tree/new") == "merged" && read(backend / "source/plain") == "incoming whole");
      TEST_CHECK(read(backend / "destination/plain") == "previous whole");
      auto undo = reversed(redo);
      TEST_CHECK(
          read(backend / "source/skipped") == "keep source" && read(backend / "destination/skipped") == "keep target"
      );
    }
    drain();
    noStages(backend);
  }
  void partialMerges(const Location& remote, const fs::path& backend) {
    for (unsigned variant = 0; variant < 3; ++variant) {
      const auto root = remote.child(std::to_string(variant));
      const auto path = backend / std::to_string(variant);
      fs::create_directories(path / "source/tree");
      fs::create_directories(path / "destination/tree");
      write(path / "source/tree/a", "first source");
      write(path / "source/tree/b", "old-source-b");
      write(path / "destination/tree/b", "old target");
      auto moved = move({root.child("source").child("tree")}, root.child("destination"), [&](const auto& conflict) {
        if (conflict.canMerge) {
          return Merge;
        }
        if (variant == 0) {
          return Skip;
        }
        if (variant == 1) {
          return Cancel;
        }
        struct stat before{};
        TEST_CHECK(::stat((path / "source/tree/b").c_str(), &before) == 0);
        write(path / "source/tree/b", "new-source-b");
        const timespec times[]{before.st_atim, before.st_mtim};
        TEST_CHECK(::utimensat(AT_FDCWD, (path / "source/tree/b").c_str(), times, 0) == 0);
        return Replace;
      });
      TEST_CHECK(!moved.receipt && moved.outcomes.front().outcome == Partial);
      TEST_CHECK(moved.outcomes.front().children.front().outcome == Copied);
      TEST_CHECK(read(path / "source/tree/a") == "first source" && read(path / "destination/tree/a") == "first source");
      TEST_CHECK(read(path / "destination/tree/b") == "old target");
      TEST_CHECK(read(path / "source/tree/b") == (variant == 2 ? "new-source-b" : "old-source-b"));
      drain();
      noStages(path);
    }
  }
  void mergeGuards(const Location& remote, const fs::path& backend) {
    for (unsigned variant = 0; variant < 5; ++variant) {
      const auto root = remote.child(std::to_string(variant));
      const auto path = backend / std::to_string(variant);
      fs::create_directories(path / "source/tree");
      fs::create_directories(path / "destination/tree");
      write(path / "source/tree/file", "incoming");
      write(path / "destination/tree/file", "previous");
      {
        auto moved = move({root.child("source").child("tree")}, root.child("destination"), mergeChoice);
        TEST_CHECK(moved.receipt);
        const auto original = history(path / "source");
        if (variant == 0) {
          struct stat old{};
          TEST_CHECK(::stat((original / "item/file").c_str(), &old) == 0);
          write(original / "item/file", "changed!");
          const timespec times[]{old.st_atim, old.st_mtim};
          TEST_CHECK(::utimensat(AT_FDCWD, (original / "item/file").c_str(), times, 0) == 0);
        } else if (variant == 1) {
          write(path / "destination/tree/file", "edited");
        } else if (variant == 2) {
          fs::rename(path / "destination/tree", path / "held-target");
          fs::create_directory(path / "destination/tree");
        } else if (variant == 3) {
          fs::rename(path / "source", path / "held-source");
          fs::create_directory(path / "source");
        } else {
          auto redo = reversed(moved.receipt);
          write(path / "source/tree/new-user-file", "preserve source addition");
          auto refused = replay(redo);
          TEST_CHECK(!refused.receipt && refused.retired);
          TEST_CHECK(read(path / "source/tree/new-user-file") == "preserve source addition");
          TEST_CHECK(read(path / "destination/tree/file") == "previous");
          continue;
        }
        auto refused = replay(moved.receipt);
        TEST_CHECK(!refused.receipt && refused.retired);
        TEST_CHECK(!fs::exists(path / "source/tree"));
        const auto retained = variant == 3 ? path / "held-source" / original.filename() : original;
        TEST_CHECK(read(retained / "item/file") == (variant == 0 ? "changed!" : "incoming"));
        if (variant == 1) {
          TEST_CHECK(read(path / "destination/tree/file") == "edited");
        }
      }
      drain(true);
      fs::remove_all(path);
    }
    // The final variant's continue intentionally releases the record before checking cleanup below.
    drain(true);
    fs::remove_all(backend / "4");
  }
  void mergeRetry(const Location& remote, const fs::path& backend) {
    fs::create_directories(backend / "source/tree");
    fs::create_directories(backend / "destination/tree");
    write(backend / "source/tree/a", "retry source");
    write(backend / "source/tree/b", "copied sibling");
    write(backend / "destination/tree/a", "previous");
    struct stat original{};
    TEST_CHECK(::stat((backend / "source/tree").c_str(), &original) == 0);
    {
      ProviderTransfer provider(
          {{remote.child("source").child("tree")}, remote.child("destination"), TransferAction::Move}
      );
      std::atomic<std::uint64_t> bytes{0};
      std::atomic<std::size_t> completed{0};
      auto first = provider.run(
          {}, bytes, completed,
          [&](const auto& conflict) {
            if (conflict.canMerge) {
              return Merge;
            }
            fs::rename(backend / "source/tree/a", backend / "held-a");
            return Replace;
          },
          nullptr, true
      );
      TEST_CHECK(first.front().outcome == Partial && provider.retryable() > 0 && !provider.takeMoveReceipt({}));
      TEST_CHECK(
          read(backend / "source/tree/b") == "copied sibling"
          && read(backend / "destination/tree/b") == "copied sibling"
      );
      fs::rename(backend / "held-a", backend / "source/tree/a");
      const timespec times[]{original.st_atim, original.st_mtim};
      TEST_CHECK(::utimensat(AT_FDCWD, (backend / "source/tree").c_str(), times, 0) == 0);
      auto retried = provider.run({}, bytes, completed, [](const auto&) { return Replace; }, nullptr, false);
      if (retried.front().error) {
        std::println(stderr, "retry: {}", retried.front().error->message);
      }
      TEST_CHECK(retried.front().outcome == Moved && !provider.takeMoveReceipt({}));
      TEST_CHECK(!fs::exists(backend / "source/tree") && read(backend / "destination/tree/a") == "retry source");
      drain();
      noStages(backend);
    }
  }

  void mergeDiscard(const Location& remote, const fs::path& backend) {
    for (unsigned undo = 0; undo < 2; ++undo) {
      const auto path = backend / std::to_string(undo);
      const auto root = remote.child(std::to_string(undo));
      fs::create_directories(path / "source/tree");
      fs::create_directories(path / "destination/tree");
      write(path / "source/tree/file", "incoming");
      write(path / "destination/tree/file", "previous");
      ProviderTransfer provider(
          {{root.child("source").child("tree")}, root.child("destination"), TransferAction::Move}
      );
      std::atomic<std::uint64_t> bytes{0};
      std::atomic<std::size_t> completed{0};
      auto result = provider.run({}, bytes, completed, mergeChoice, nullptr, true);
      TEST_CHECK(result.front().outcome == Moved);
      {
        auto receipt = provider.takeMoveReceipt({});
        TEST_CHECK(receipt);
        if (undo) {
          receipt = reversed(receipt);
        }
      }
      // The completed report can outlive history, without retaining its inactive data.
      drain();
      noStages(path);
      TEST_CHECK(read(path / "destination/tree/file") == (undo ? "previous" : "incoming"));
      TEST_CHECK(undo ? read(path / "source/tree/file") == "incoming" : !fs::exists(path / "source/tree"));
      TEST_CHECK(provider.retryable() == 0);
    }
  }
  void lostMergeReply(const Location& remote, const fs::path& backend, const fs::path& arm, unsigned variant) {
    TEST_CHECK(variant < 5);
    fs::create_directories(backend / "source/tree");
    fs::create_directories(backend / "destination/tree");
    write(backend / "source/tree/file", "incoming");
    write(backend / "destination/tree/file", "previous");
    write(backend / "destination/tree/keep", "unrelated");
    {
      auto done = move({remote.child("source").child("tree")}, remote.child("destination"), [&](const auto& conflict) {
        if (variant == 0 && !conflict.canMerge) {
          write(arm, (backend / "source/tree").native());
        }
        return mergeChoice(conflict);
      });
      const auto original = history(backend / "source");
      TEST_CHECK(read(backend / "destination/tree/file") == "incoming");
      if (variant == 0) {
        TEST_CHECK(!done.receipt && done.outcomes.front().outcome == Partial);
        TEST_CHECK(done.outcomes.front().error->message.contains(original.filename().native()));
        TEST_CHECK(done.outcomes.front().children.front().outcome == Copied);
      } else {
        TEST_CHECK(done.receipt && done.outcomes.front().outcome == Moved);
        auto receipt = variant >= 3 ? reversed(done.receipt) : done.receipt;
        const auto trigger = variant == 1 ? original / "item"
            : variant == 3                ? backend / "source/tree"
                                          : backend / "destination/tree/file";
        write(arm, trigger.native());
        auto uncertain = replay(receipt);
        TEST_CHECK(!uncertain.receipt && uncertain.retired);
        TEST_CHECK(std::ranges::any_of(uncertain.outcomes, [](const auto& item) { return item.outcome == Partial; }));
        TEST_CHECK(std::ranges::any_of(uncertain.outcomes, [](const auto& item) {
          return item.error && item.error->message.contains("Inspect");
        }));
      }
      const auto restored = variant == 1 || variant == 2;
      TEST_CHECK(read((restored ? backend / "source/tree" : original / "item") / "file") == "incoming");
      TEST_CHECK(fs::exists(backend / "source/tree") == restored);
      TEST_CHECK(read(backend / "destination/tree/keep") == "unrelated");
      // A dropped reply can occur between the two replacement renames.
      const auto active = backend / "destination/tree/file";
      if (variant <= 1) {
        TEST_CHECK(read(active) == "incoming");
      } else if (variant == 3) {
        TEST_CHECK(read(active) == "previous");
      } else {
        TEST_CHECK(!fs::exists(active));
      }
      bool previous = false, copied = false;
      for (const auto& file : fs::recursive_directory_iterator(backend / "destination/tree")) {
        if (file.is_regular_file()) {
          previous |= read(file.path()) == "previous";
          copied |= read(file.path()) == "incoming";
        }
      }
      TEST_CHECK(previous && copied);
    }
    drain(true);
    TEST_CHECK(fs::exists(history(backend / "source") / "history-token"));
  }

  void run(const Location& remote, const fs::path& backend) {
    basic(remote.child("basic"), backend / "basic");
    replacements(remote.child("replacements"), backend / "replacements");
    collisions(remote.child("collisions"), backend / "collisions");
    guards(remote.child("guards"), backend / "guards");
    preflight(remote.child("preflight"), backend / "preflight");
    excluded(remote.child("excluded"), backend / "excluded");
    overlapsAndLimits(remote.child("limits"), backend / "limits");
    mergeHistory(remote.child("merges"), backend / "merges");
    emptyAndMixedMerges(remote.child("mixed-merges"), backend / "mixed-merges");
    partialMerges(remote.child("partial-merges"), backend / "partial-merges");
    mergeGuards(remote.child("merge-guards"), backend / "merge-guards");
    mergeRetry(remote.child("merge-retry"), backend / "merge-retry");
    mergeDiscard(remote.child("merge-discard"), backend / "merge-discard");
  }
} // namespace
int main(int argc, char* argv[]) {
  GlibPollSource glib;
  if (argc == 6 && std::string_view(argv[1]) == "--lost-reply") {
    const auto remote = Location::fromUri(argv[2]);
    TEST_CHECK(remote.uri().starts_with("sftp:"));
    mount(remote);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("lost-move-") + uuid.get();
    lostReply(remote.child(name), fs::path(argv[3]) / name, argv[4], static_cast<unsigned>(std::stoul(argv[5])));
  } else if (argc == 6 && std::string_view(argv[1]) == "--lost-merge") {
    const auto remote = Location::fromUri(argv[2]);
    TEST_CHECK(remote.uri().starts_with("sftp:"));
    mount(remote);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("lost-merge-") + uuid.get();
    lostMergeReply(remote.child(name), fs::path(argv[3]) / name, argv[4], static_cast<unsigned>(std::stoul(argv[5])));
  } else if (argc == 4 && std::string_view(argv[1]) == "--provider") {
    const auto remote = Location::fromUri(argv[2]);
    TEST_CHECK(remote.uri().starts_with("sftp:"));
    mount(remote);
    const luciole::GString uuid(g_uuid_string_random());
    const auto name = std::string("move-history-") + uuid.get();
    const auto backend = fs::path(argv[3]) / name;
    fs::create_directories(backend);
    run(remote.child(name), backend);
  } else {
    TEST_CHECK(argc == 1);
    const luciole::GString tmp(g_dir_make_tmp("luciole-provider-moves-XXXXXX", nullptr));
    TEST_CHECK(tmp);
    const fs::path backend(tmp.get());
    run(Location::fromPath(backend.native()), backend);
    fs::remove_all(backend);
  }
  std::println("Provider Move history checks passed");
}
