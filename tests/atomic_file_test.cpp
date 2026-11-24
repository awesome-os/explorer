#include "fs/atomic_file.h"
#include "test_check.h"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
  namespace fs = std::filesystem;
  using luciole::AtomicWriteState;

  struct Fixture {
    fs::path directory;
    Fixture() {
      auto pattern = (fs::temp_directory_path() / "luciole-atomic-test-XXXXXX").native();
      const char* created = ::mkdtemp(pattern.data());
      TEST_CHECK(created != nullptr);
      directory = created;
    }
    ~Fixture() {
      std::error_code ignored;
      fs::remove_all(directory, ignored);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
  };

  std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void plainWrite(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    TEST_CHECK(output.good());
  }

  luciole::AtomicWriteResult save(const fs::path& path, const std::string& text) {
    return luciole::updateTextFileAtomic(
        path, [&](const auto&) -> std::expected<std::optional<std::string>, std::string> { return text; }
    );
  }

  void noTemporaryFiles(const fs::path& directory) {
    for (const auto& item : fs::directory_iterator(directory)) {
      const auto name = item.path().filename().native();
      TEST_CHECK(!name.starts_with(".settings.toml.tmp."));
    }
  }

  void testDurableReplacementAndNoop() {
    Fixture fixture;
    const auto invalid = fs::path((fixture.directory / "settings.toml").native() + std::string("\0suffix", 7));
    TEST_CHECK(
        save(invalid, "invalid").state == AtomicWriteState::NotCommitted
        && !fs::exists(fixture.directory / "settings.toml")
    );
    const auto parent = fixture.directory / "new" / "nested";
    const auto path = parent / "settings.toml";
    auto result = save(path, "first");
    TEST_CHECK(result.state == AtomicWriteState::Durable && result.error.empty() && read(path) == "first");
    struct stat info{};
    TEST_CHECK(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    TEST_CHECK(::stat(parent.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
    plainWrite(parent / "settings.toml.tmp", "unrelated fixed temporary name");
    result = save(path, "second");
    TEST_CHECK(result.state == AtomicWriteState::Durable && read(path) == "second");
    TEST_CHECK(read(parent / "settings.toml.tmp") == "unrelated fixed temporary name");
    TEST_CHECK(::stat(path.c_str(), &info) == 0);
    const auto inode = info.st_ino;
    result = save(path, "second");
    TEST_CHECK(result.state == AtomicWriteState::Unchanged && ::stat(path.c_str(), &info) == 0 && info.st_ino == inode);
    TEST_CHECK(
        save(path, std::string(256 * 1024 + 1, 'x')).state == AtomicWriteState::NotCommitted && read(path) == "second"
    );
    noTemporaryFiles(parent);
  }

  void testRejectedTargetsAndConflicts() {
    Fixture fixture;
    const auto path = fixture.directory / "settings.toml";
    const auto outside = fixture.directory / "base.toml";
    plainWrite(outside, "base");
    fs::create_symlink(outside, path);
    TEST_CHECK(
        save(path, "candidate").state == AtomicWriteState::NotCommitted
        && read(outside) == "base"
        && fs::is_symlink(path)
    );
    fs::remove(path);
    fs::create_hard_link(outside, path);
    TEST_CHECK(save(path, "candidate").state == AtomicWriteState::NotCommitted && read(outside) == "base");
    fs::remove(path);
    fs::create_directory(path);
    TEST_CHECK(save(path, "candidate").state == AtomicWriteState::NotCommitted);
    fs::remove(path);
    TEST_CHECK(::mkfifo(path.c_str(), 0600) == 0);
    TEST_CHECK(save(path, "candidate").state == AtomicWriteState::NotCommitted);
    fs::remove(path);
    plainWrite(path, "old");
    TEST_CHECK(::chmod(path.c_str(), 0444) == 0);
    TEST_CHECK(save(path, "candidate").state == AtomicWriteState::NotCommitted && read(path) == "old");
    ::chmod(path.c_str(), 0600);
    const auto conflict = luciole::updateTextFileAtomic(
        path, [&](const auto& current) -> std::expected<std::optional<std::string>, std::string> {
          TEST_CHECK(current == "old");
          plainWrite(path, "external edit");
          return "candidate";
        }
    );
    TEST_CHECK(conflict.state == AtomicWriteState::NotCommitted && read(path) == "external edit");
    fs::remove(path);
    const auto appeared = luciole::updateTextFileAtomic(
        path, [&](const auto& current) -> std::expected<std::optional<std::string>, std::string> {
          TEST_CHECK(!current);
          plainWrite(path, "external new file");
          return "candidate";
        }
    );
    TEST_CHECK(appeared.state == AtomicWriteState::NotCommitted && read(path) == "external new file");
    noTemporaryFiles(fixture.directory);
  }

  void testLockAndFailureCleanup() {
    Fixture fixture;
    const auto path = fixture.directory / "settings.toml";
    TEST_CHECK(save(path, "old").state == AtomicWriteState::Durable);
    const int lock = ::open((fixture.directory / ".settings.toml.lock").c_str(), O_RDWR | O_CLOEXEC);
    TEST_CHECK(lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == 0);
    const auto busy = save(path, "blocked");
    TEST_CHECK(busy.state == AtomicWriteState::NotCommitted && read(path) == "old" && !busy.error.empty());
    const auto contender = ::fork();
    TEST_CHECK(contender >= 0);
    if (contender == 0) {
      const auto result = save(path, "blocked child");
      ::_exit(result.state == AtomicWriteState::NotCommitted ? 0 : 1);
    }
    int contenderStatus = 0;
    TEST_CHECK(
        ::waitpid(contender, &contenderStatus, 0) == contender
        && WIFEXITED(contenderStatus)
        && WEXITSTATUS(contenderStatus) == 0
    );
    ::close(lock);
    TEST_CHECK(save(path, "after lock").state == AtomicWriteState::Durable);

    // A real storage failure before rename must preserve the old file and
    // remove this transaction's temporary file. Limits affect only the child.
    const auto child = ::fork();
    TEST_CHECK(child >= 0);
    if (child == 0) {
      std::signal(SIGXFSZ, SIG_IGN);
      const rlimit limit{.rlim_cur = 0, .rlim_max = 0};
      if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) {
        ::_exit(2);
      }
      const auto result = save(path, "must fail while writing");
      ::_exit(result.state == AtomicWriteState::NotCommitted ? 0 : 1);
    }
    int status = 0;
    TEST_CHECK(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    TEST_CHECK(read(path) == "after lock");
    noTemporaryFiles(fixture.directory);
    fs::remove(fixture.directory / ".settings.toml.lock");
    const auto untouched = fixture.directory / "lock-target";
    plainWrite(untouched, "leave me alone");
    fs::create_symlink(untouched, fixture.directory / ".settings.toml.lock");
    TEST_CHECK(save(path, "candidate").state == AtomicWriteState::NotCommitted && read(untouched) == "leave me alone");
  }
} // namespace

int main() {
  try {
    testDurableReplacementAndNoop();
    testRejectedTargetsAndConflicts();
    testLockAndFailureCleanup();
    std::println("atomic file checks passed");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "atomic file test: {}", error.what());
    return 1;
  }
}
