#include "clipboard/file_payload.h"
#include "fs/gio_interop.h"
#include "test_check.h"

#include <print>
#include <stdexcept>

namespace {
  void testRoundTrip() {
    using namespace luciole;
    const std::string raw = std::string("/tmp/clipboard/space #?%\n") + static_cast<char>(0xff) + ".txt";
    const FilePayload input{{Location::fromPath(raw), Location::fromPath("/tmp/clipboard/日本語.txt")}, true};
    for (const auto mime : {kUriListMime, kGnomeFilesMime}) {
      const auto encoded = encodeFilePayload(input, mime);
      TEST_CHECK(encoded.has_value());
      const auto parsed = parseFilePayload(mime, *encoded);
      TEST_CHECK(parsed && parsed->locations == input.locations);
      TEST_CHECK(parsed->cut == (mime == kGnomeFilesMime));
      TEST_CHECK(!encoded->contains(static_cast<char>(0xff)));
    }
    TEST_CHECK(*encodeFilePayload(input, kKdeCutMime) == "1");
    auto copy = input;
    copy.cut = false;
    TEST_CHECK(*encodeFilePayload(copy, kKdeCutMime) == "0");
    const auto commented = parseFilePayload(kUriListMime, "# selection\r\n\r\nfile:///tmp/a%20b\r\n");
    TEST_CHECK(commented && commented->locations.front().nativePath() == "/tmp/a b");
  }
  void testRemoteRoundTrip() {
    const luciole::FilePayload remote{
        {luciole::Location::fromUri("sftp://user@example.invalid/path/a%20b%FF.txt"),
         luciole::Location::fromUri("smb://server.invalid/share/caf%C3%A9%20%23.txt"),
         luciole::Location::fromUri("dav://server.invalid/files/space%20%25.txt"),
         luciole::Location::fromUri("davs://server.invalid/files/%E6%97%A5%E6%9C%AC%E8%AA%9E.txt")},
        true
    };
    for (const auto mime : {luciole::kUriListMime, luciole::kGnomeFilesMime}) {
      const auto encoded = luciole::encodeFilePayload(remote, mime);
      TEST_CHECK(encoded);
      const auto decoded = luciole::parseFilePayload(mime, *encoded);
      TEST_CHECK(decoded && decoded->locations == remote.locations);
    }
  }
  void testRejection() {
    using namespace luciole;
    for (const auto* bytes :
         {"", "# no files\n", "/tmp/plain-path", "file:///tmp/a b", "file:///tmp/a\rgarbage", "file:///tmp/a%00b",
          "file:///tmp/a#fragment", "file:///tmp/a?query", "file:///tmp/a\nfile:///tmp/a",
          "file:///tmp/good\nnot-a-uri"}) {
      TEST_CHECK(!parseFilePayload(kUriListMime, bytes));
    }
    TEST_CHECK(!parseFilePayload(kGnomeFilesMime, "move\nfile:///tmp/a"));
    TEST_CHECK(!parseFilePayload(kGnomeFilesMime, "cut\n"));
    TEST_CHECK(!parseFilePayload(kGnomeFilesMime, "copy\nfile:///tmp/a\n\nfile:///tmp/b"));
    TEST_CHECK(!parseFilePayload(kUriListMime, std::string("file:///tmp/a\0b", 15)));
    TEST_CHECK(!parseFilePayload(kUriListMime, std::string(kFilePayloadLimit + 1, '#')));
    TEST_CHECK(!encodeFilePayload({}, kUriListMime));
    std::string many;
    for (std::size_t i = 0; i <= kFilePayloadCount; ++i) {
      many += "file:///tmp/item" + std::to_string(i) + "\n";
    }
    TEST_CHECK(!parseFilePayload(kUriListMime, many));
  }
  void testPartialMoves() {
    using namespace luciole;
    FilePayload selection{{}, true};
    for (int i = 0; i < 7; ++i) {
      selection.locations.push_back(Location::fromPath("/tmp/source/" + std::to_string(i)));
    }
    const std::vector<TransferResult> results{
        {selection.locations[0], TransferOutcome::Moved, {}},
        {selection.locations[1], TransferOutcome::Skipped, {}},
        {selection.locations[2], TransferOutcome::Failed,
         luciole::FileError{luciole::FileErrorKind::Other, "fixture failure"}},
        {selection.locations[3], TransferOutcome::Partial,
         luciole::FileError{luciole::FileErrorKind::Other, "source removal incomplete"}},
        {selection.locations[4], TransferOutcome::Cancelled, {}},
        {selection.locations[5], TransferOutcome::Copied, {}},
    };
    const auto remaining = remainingCutFiles(selection, results);
    TEST_CHECK(remaining.cut && remaining.locations.size() == 6);
    TEST_CHECK(
        remaining.locations.front() == selection.locations[1] && remaining.locations.back() == selection.locations[6]
    );
    selection.cut = false;
    TEST_CHECK(remainingCutFiles(selection, results).locations == selection.locations);
  }
} // namespace
int main() {
  try {
    testRoundTrip();
    testRejection();
    testRemoteRoundTrip();
    testPartialMoves();
    std::println("file payload tests passed");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "{}", error.what());
    return 1;
  }
}
