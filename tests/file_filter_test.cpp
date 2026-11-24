#include "fs/file_filter.h"
#include "test_check.h"

#include <exception>
#include <print>
#include <vector>

namespace {
  using luciole::FileFilter;
  using luciole::FilePattern;
  using Kind = luciole::FilePattern::Kind;

  void testGlobs() {
    const FileFilter png(std::vector<FilePattern>{{.kind = Kind::Glob, .pattern = "*.png"}});
    TEST_CHECK(png.matches("a.png", nullptr));
    TEST_CHECK(!png.matches("a.PNG", nullptr));
    const FileFilter folded(std::vector<FilePattern>{{.kind = Kind::Glob, .pattern = "*.[pP][nN][gG]"}});
    TEST_CHECK(folded.matches("a.PNG", nullptr) && folded.matches("a.png", nullptr));
    TEST_CHECK(!FileFilter(std::vector<FilePattern>{}).matches("a.png", nullptr));
  }

  void testMimeTypes() {
    const FileFilter png(std::vector<FilePattern>{{.kind = Kind::MimeType, .pattern = "image/png"}});
    TEST_CHECK(png.matches("x.png", nullptr));
    TEST_CHECK(!png.matches("notes.txt", nullptr));
    const FileFilter images(std::vector<FilePattern>{{.kind = Kind::MimeType, .pattern = "image/*"}});
    TEST_CHECK(images.matches("photo", "image/jpeg"));
    TEST_CHECK(!images.matches("photo.jpg", "text/plain"));
    const FileFilter text(std::vector<FilePattern>{{.kind = Kind::MimeType, .pattern = "text/plain"}});
    TEST_CHECK(text.matches("main.c", "text/x-csrc"));
    const FileFilter either(
        std::vector<FilePattern>{
            {.kind = Kind::Glob, .pattern = "*.txt"}, {.kind = Kind::MimeType, .pattern = "image/png"}
        }
    );
    TEST_CHECK(either.matches("notes.txt", nullptr) && either.matches("x.png", nullptr));
  }
} // namespace

int main() {
  try {
    testGlobs();
    testMimeTypes();
    std::println("file filter checks passed");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "file filter test: {}", error.what());
    return 1;
  }
}
